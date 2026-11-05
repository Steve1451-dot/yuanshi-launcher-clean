#include "endfield/hg_api.hpp"
#include "endfield/account_policy.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include "endfield/endfield_profile.hpp"
#include "endfield/endfield_sign.hpp"
#include "endfield/hg_login.hpp"
#include "endfield/jwe.hpp"
#include "http_client.hpp"
#include <mbedtls/md.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace cloudgenshin::endfield::api {
namespace {

// Preserve the working HTTP transport UA independently of the cloud API and
// game bridge profile. Passport uses its own verified account-SDK headers.
constexpr std::string_view kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Safari/537.36";
// Cloud headers describe the actual native host, never an invented phone.
constexpr std::string_view kClientDeviceModel = "Nintendo Switch";
constexpr std::string_view kClientOsVersion = "unknown";

constexpr std::size_t kMaxBodyBytes = 1U << 20U;
constexpr long kTimeoutMs = 15000;
constexpr long long kClockSkewRetrySeconds = 300;  // web tj
constexpr int kTransportRetryDelayMs = 300;        // web tU for code -1

// Server clock offset in seconds, calibrated from response timestamps exactly
// like the web client's tB: offset = local_now - server_timestamp.
std::atomic<long long> gClockOffsetSeconds{0};

long long NowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

long long NowMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// Web n = floor(Date.now()/1000) - tB (tB stays 0 until the first calibrated
// response).
long long RequestTimestampSeconds() {
    const long long offset = gClockOffsetSeconds.load(std::memory_order_relaxed);
    return offset != 0 ? NowSeconds() - offset : NowSeconds();
}

bool RetryableServerError(long code) {
    return code == 408 || code == -1 || (code >= 500 && code <= 599);
}

bool TimestampError(long code) {
    return code == 11121 || code == 11201;
}

std::vector<std::string> BuildRequestHeaders(const std::string& method,
                                             const std::string& path,
                                             const std::string& body, bool has_body,
                                             const std::string& login_session_id) {
    const long long timestamp = RequestTimestampSeconds();
    const std::string timestamp_text = std::to_string(timestamp);
    const std::string authorization =
        login_session_id.empty() ? std::string() : "Bearer " + login_session_id;
    const auto signature = BuildSignature(method, path, timestamp_text, authorization,
                                          has_body ? std::optional<std::string>(body)
                                                   : std::nullopt);

    std::vector<std::string> headers;
    // Endfield's JSON replies are small. Avoid compressed persistent
    // responses that can stall after HTTP 200 on the Switch curl backend.
    headers.emplace_back("Accept-Encoding: identity");
    headers.emplace_back("Connection: close");
    if (has_body) headers.emplace_back("Content-Type: application/json");
    headers.emplace_back("X-APP-CODE: " + std::string(kServerAppCode));
    headers.emplace_back("X-Client-App-Version: " + std::string(kClientAppVersion));
    headers.emplace_back("X-Client-Platform: " + std::string(kClientPlatform));
    headers.emplace_back("X-Client-Device-Model: " + std::string(kClientDeviceModel));
    headers.emplace_back("X-Client-Os-Version: " + std::string(kClientOsVersion));
    headers.emplace_back("X-Timestamp: " + timestamp_text);
    if (signature) headers.emplace_back("X-Signature: " + *signature);
    if (!authorization.empty()) headers.emplace_back("Authorization: " + authorization);
    return headers;
}

// One signed request against kCloudServerUrl with the web retry policy: at
// most one retry for transient failures (HTTP/transport errors, 408, -1, 5xx)
// and at most one for timestamp errors / stale responses. The server clock
// offset is re-calibrated from every response that carries a timestamp.
Envelope PerformRequest(const std::string& method, const std::string& path,
                        const std::string& body, bool has_body,
                        const std::string& login_session_id,
                        const std::atomic_bool* cancelled=nullptr,long timeout_ms=kTimeoutMs) {
    const std::string url = std::string(kCloudServerUrl) + path;
    opennow::HttpClient client;
    opennow::HttpTransferControl control;
    control.timeout_ms = timeout_ms;
    control.max_body_bytes = kMaxBodyBytes;
    control.cancelled=cancelled;

    bool server_retry_used = false;
    bool timestamp_retry_used = false;
    for (;;) {
        if(cancelled && cancelled->load()) {Envelope stopped;stopped.code=-2;return stopped;}
        const auto headers = BuildRequestHeaders(method, path, body, has_body, login_session_id);
        opennow::HttpResponse response;
        try {response=client.Request(method,url,std::string(kUserAgent),headers,body,"",control);}
        catch(const opennow::HttpRequestError& error) {
            Envelope failed;
            failed.code=cancelled && cancelled->load() ? -2 : -1000-static_cast<long>(error.curl_code);
            failed.http=error.http_status;failed.message="cloud transport failed";
            return failed;
        }

        Envelope envelope;
        if (response.status_code >= 200 && response.status_code < 300) {
            envelope = ParseEnvelope(response.body);
        } else {
            envelope.code = -1;
            envelope.message = "HTTP " + std::to_string(response.status_code);
        }
        envelope.http = response.status_code;
        // A successful dispatch may race with Cancel. Preserve its session id
        // so the owner can release that allocation; suppress retries on errors.
        if(cancelled && cancelled->load() && !envelope.success) {envelope.code=-2;return envelope;}

        const long long now = NowSeconds();
        const long long offset = gClockOffsetSeconds.load(std::memory_order_relaxed);
        const long long server_time = envelope.timestamp;
        const bool stale =
            server_time > 0 && now - server_time - offset > kClockSkewRetrySeconds;
        if (server_time > 0)
            gClockOffsetSeconds.store(now - server_time, std::memory_order_relaxed);

        if (!envelope.success) {
            // A cancelled flow must not sleep through a retry window.
            if(cancelled && cancelled->load()) {envelope.code=-2;return envelope;}
            if (!server_retry_used && RetryableServerError(envelope.code)) {
                server_retry_used = true;
                if (envelope.code == -1)
                    std::this_thread::sleep_for(std::chrono::milliseconds(kTransportRetryDelayMs));
                continue;
            }
            if (!timestamp_retry_used && (TimestampError(envelope.code) || stale)) {
                timestamp_retry_used = true;
                continue;
            }
        }
        return envelope;
    }
}

ApiError ToApiError(const Envelope& envelope) {
    ApiError error;
    error.code = envelope.code;
    error.http = envelope.http;
    error.message = envelope.message;
    return error;
}

}  // namespace

ExchangeResult Exchange(const std::string& hg_token, const std::string& u8_token,
                        const std::atomic_bool* cancelled) {
    ExchangeResult result;
    if (hg_token.empty() || hg_token.size() > 4096 || u8_token.empty() || u8_token.size()>4096) {
        result.error = {-9, "cloud credentials missing or too long"};
        return result;
    }
    EcdhP256 key;
    if (!key.valid()) {
        result.error = {-10, "ephemeral key generation failed"};
        return result;
    }
    const std::string body = BuildExchangeBody(hg_token, u8_token, key.public_jwk());
    const Envelope envelope = PerformRequest("POST", "/api/v1/user/exchange", body, true, "",cancelled);
    if (!envelope.success) {
        result.error = ToApiError(envelope);
        if (result.error.message.empty()) result.error.message = "exchange failed";
        return result;
    }

    std::string diagnostic;
    auto session = ParseLoginSessionData(envelope.data_json, &key, &diagnostic);
    if (!session) {
        result.error = {-12, "exchange response missing tenant data"};
        result.error.http = envelope.http;
        return result;
    }
    session->tenant_jwe_alg_enc = std::move(diagnostic);
    result.session = std::move(*session);
    result.error.http = envelope.http;
    return result;
}

DispatchResult Dispatch(const std::string& login_session_id, const DispatchInputs& inputs,
                        const std::atomic_bool* cancelled) {
    DispatchResult result;
    if (login_session_id.empty() || inputs.tenant_key.empty() || inputs.game_id.empty()) {
        result.error = {-1, "dispatch arguments missing"};
        return result;
    }
    const std::string body = BuildDispatchBody(inputs);
    const Envelope envelope =
        PerformRequest("POST", "/api/v1/session/dispatch", body, true, login_session_id,cancelled);
    if (!envelope.success) {
        result.error = ToApiError(envelope);
        return result;
    }
    auto state = ParseQueueState(envelope.data_json);
    if (!state) {
        result.error = {-1, "dispatch response parse failed"};
        return result;
    }
    result.state = std::move(*state);
    return result;
}

PollResult Poll(const std::string& login_session_id, const std::string& session_id,
                const std::atomic_bool* cancelled) {
    PollResult result;
    if (login_session_id.empty() || session_id.empty()) {
        result.error = {-1, "poll arguments missing"};
        return result;
    }
    const std::string path = "/api/v1/session/poll/" + session_id;
    const Envelope envelope = PerformRequest("GET", path, "", false, login_session_id,cancelled);
    if (!envelope.success) {
        result.error = ToApiError(envelope);
        return result;
    }
    auto state = ParseQueueState(envelope.data_json);
    if (!state) {
        result.error = {-1, "poll response parse failed"};
        return result;
    }
    result.state = std::move(*state);
    return result;
}

ApiError Cancel(const std::string& login_session_id, const std::string& session_id) {
    if (login_session_id.empty() || session_id.empty()) return {-1, "cancel arguments missing"};
    const std::string body = BuildCancelBody(session_id);
    const Envelope envelope =
        PerformRequest("POST", "/api/v1/session/cancel", body, true, login_session_id,nullptr,5000);
    if (envelope.success) return {};
    return ToApiError(envelope);
}

NodeListResult GetNodeList(const std::string& login_session_id,const std::string& tenant_key,
                           const std::atomic_bool* cancelled) {
    NodeListResult result;
    if (tenant_key.empty() || tenant_key.size()>4096) {
        result.error = {-1, "invalid deploy node arguments"};
        return result;
    }
    // SDK getNodeList(callback,error) has no instance-game parameter: it
    // loads /ping/md5(tenantKey+"H5").html on the two official config CDNs.
    // tenant_params is opaque here and must not be split into a game id.
    const auto* md=mbedtls_md_info_from_type(MBEDTLS_MD_MD5);
    std::array<unsigned char,16> digest{};
    const std::string identifier=tenant_key+"H5";
    if(!md || mbedtls_md(md,reinterpret_cast<const unsigned char*>(identifier.data()),
                         identifier.size(),digest.data())!=0) {
        result.error={-1,"node identifier hashing failed"};return result;
    }
    constexpr char hex[]="0123456789abcdef";
    std::string path="/ping/";
    for(const auto byte:digest) {path+=hex[byte>>4];path+=hex[byte&15];}
    path+=".html?t="+std::to_string(NowMilliseconds());
    opennow::HttpClient client;
    opennow::HttpTransferControl control;
    control.timeout_ms = 3000;
    control.max_body_bytes = kMaxBodyBytes;
    control.cancelled=cancelled;
    std::vector<DeployNode> nodes;
    for(const auto* host:{"https://paas-sdk-config.vlinkcloud.cn","https://paas-sdk-config-ks.vlinkcloud.cn"}) {
        if(cancelled && cancelled->load()) {result.error={-2,"cancelled"};return result;}
        try {
            const auto response=client.Request("GET",std::string(host)+path,std::string(kUserAgent),
                {"Accept-Encoding: identity","Connection: close"},"","",control);
            result.error.http=response.status_code;
            if(response.status_code>=200 && response.status_code<300 && ParseDeployNodeList(response.body,&nodes)) {
                result.deploy_ok=true;result.error={};break;
            }
            result.error.code=-1;result.error.message="node configuration unavailable";
        } catch(const opennow::HttpRequestError& error) {
            result.error={-1000-static_cast<long>(error.curl_code),"node configuration transport failed"};
            result.error.http=error.http_status;
        }
    }
    if(!result.deploy_ok) return result;

    // Wait-time enrichment needs the login session; without one the deploy
    // list is still usable (delay stays 0).
    if (!login_session_id.empty()) {
        const std::string body = BuildNodeListBody(nodes);
        const Envelope envelope =
            PerformRequest("POST", "/api/v1/game/node_list", body, true, login_session_id,cancelled);
        if (!envelope.success) {
            result.error = ToApiError(envelope);
        } else {
            std::vector<DeployNode> enriched;
            if (ParseNodeListData(envelope.data_json, &enriched)) nodes = std::move(enriched);
        }
    }

    // Preserve the config order when measured delays are unavailable.
    std::stable_sort(nodes.begin(), nodes.end(),
                     [](const DeployNode& a, const DeployNode& b) { return a.delay < b.delay; });
    result.nodes = std::move(nodes);
    return result;
}

namespace {

// Account-side hosts and the binding service app code from the official
// account SDK (hg-account-web-sdk 3.5.0, bundled module constants).
constexpr std::string_view kPassportHostUrlDetail = "https://as.hypergryph.com";
constexpr std::string_view kBindingHostUrlDetail =
    "https://binding-api-account-prod.hypergryph.com";
constexpr std::string_view kBindingServiceAppCode = "be36d44aa36bfb5b";
// The binding list is queried with the Endfield u8 app code (SDK constant
// u8AppCodeMap.endfield); the per-game entries carry the account uid.
constexpr std::string_view kEndfieldU8AppCode = "4df8f5a7c2ad711b497a";

// The account chain (grant + u8_ticket) uses the passport/host envelope:
// {status, msg, data:{token}} — the same shape the QR swap answers with.
// The SDK reads data.token through its own unwrap; this helper accepts the
// documented status/code envelope and fails otherwise.
std::optional<std::string> ParseTokenEnvelope(const std::string& body) {
    return account::Token(body);
}

// One plain HTTPS request against an account host; no X-Signature stack.
opennow::HttpResponse PostAccount(const std::string& url, const std::string& body,
                                  long& http_status,const std::atomic_bool* cancelled=nullptr,
                                  bool close_response=false) {
    opennow::HttpClient client;
    opennow::HttpTransferControl control;
    control.timeout_ms = kTimeoutMs;
    control.max_body_bytes = kMaxBodyBytes;
    control.cancelled = cancelled;
    auto headers=account::SessionHeaders(true);
    if(close_response) {
        // The small U8 reply needs no compression or persistent HTTP socket.
        // Explicit framing avoids an HTTP 200 transfer hanging until timeout
        // with the Switch port's older curl/network backend.
        headers.emplace_back("Accept-Encoding: identity");
        headers.emplace_back("Connection: close");
    }
    const auto response =
        client.Request("POST", url, std::string(kUserAgent),
                       headers, body, "", control);
    http_status = response.status_code;
    return response;
}

opennow::HttpResponse GetAccount(const std::string& url, long& http_status,
                                const std::atomic_bool* cancelled=nullptr) {
    opennow::HttpClient client;
    opennow::HttpTransferControl control;
    control.timeout_ms = kTimeoutMs;
    control.max_body_bytes = kMaxBodyBytes;
    control.cancelled = cancelled;
    const auto response =
        client.Request("GET", url, std::string(kUserAgent),account::SessionHeaders(false), "", "", control);
    http_status = response.status_code;
    return response;
}

// binding_list answers {status|code, data:{list:[{appCode, bindingList:[...]}]}}
// (SDK: i.list.find(appCode).bindingList). Entries carry uid plus isDefault /
// isOfficial flags; pick the default entry, else the last official one, else
// the first entry that has a uid.
std::optional<std::string> FindBindingUid(const std::string& body) {
    return account::BindingUid(body,kEndfieldU8AppCode);
}

}  // namespace

CloudCredentialsResult FetchCloudCredentials(const std::string& passport_token,
                                             const std::atomic_bool* cancelled,
                                             const std::string& device_token) {
    CloudCredentialsResult result;
    if(passport_token.empty() || passport_token.size()>4096 || device_token.size()>8192) {
        result.error={-9,"passport token missing or too long"};return result;
    }
    const auto stopped=[&]{return cancelled && cancelled->load();};
    if(stopped()) {result.error={-2,"cancelled"};return result;}
    const auto request=[&](const std::string& url,const nlohmann::json& body,bool close_response=false) {
        long http=0;
        try {return PostAccount(url,body.dump(),http,cancelled,close_response);}
        catch(const opennow::HttpRequestError& error) {
            // Numeric diagnostics only; exception text is never logged.
            result.error={stopped() ? -2 : -1000-static_cast<long>(error.curl_code),
                          "account transport failed"};
            result.error.http=error.http_status;
        } catch(const std::runtime_error&) {
            result.error={stopped() ? -2 : -20,"account transport failed"};
        }
        return opennow::HttpResponse{};
    };
    const auto read_token=[&](const opennow::HttpResponse& response) {
        if(result.error.code!=0) return std::optional<std::string>{};
        result.error.http=response.status_code;
        if(response.status_code<200 || response.status_code>=300) {
            result.error.code=-20;result.error.message="account request failed";
            return std::optional<std::string>{};
        }
        const auto token=ParseTokenEnvelope(response.body);
        if(!token || token->size()>4096) {
            const auto reply=account::Parse(response.body);
            result.error.code=reply.parsed && reply.status!=0 ? reply.status : -21;
            result.error.message="account response missing token";
            return std::optional<std::string>{};
        }
        return token;
    };
    // SDK 1192 grantApp() uses type=OTHER (1), not a binding-service grant.
    const bool web_client=!device_token.empty();
    nlohmann::json grant_body={{"token",passport_token},
        {"appCode",std::string(login::kPassportAppCode)},{"type",web_client?0:1}};
    if(web_client) grant_body["deviceToken"]=device_token;
    const auto grant=request(std::string(kPassportHostUrlDetail)+"/user/oauth2/v2/grant",grant_body,true);
    const auto cloud_token=read_token(grant);
    if(!cloud_token) return result;
    std::string game_auth_code;
    if(web_client) {
        const auto granted=account::Parse(grant.body);
        if(granted.data.is_object()) {
            const auto code=granted.data.find("code");
            if(code!=granted.data.end() && code->is_string()) game_auth_code=code->get<std::string>();
        }
        if(game_auth_code.empty() || game_auth_code.size()>4096) {
            result.error.code=-21;result.error.message="client grant missing game code";return result;
        }
    }
    if(stopped()) {result.error={-2,"cancelled"};return result;}
    result.stage=CloudCredentialsResult::Stage::GameTicket;
    // SDK U8.API.authByHG -> L(appCode, OFFICIAL, {type:0,token:passport}).
    nlohmann::json ticket_body={{"appCode",std::string(kEndfieldU8AppCode)},
        {"channelMasterId",1},{"channelToken",{{"type",0},{"token",passport_token}}}};
    if(web_client) {
        // Official z -> T: redeem this very CLIENT code for U8, then keep the
        // same code for launch_info (K.B4). Do not mix an unrelated OTHER
        // passport ticket and a later unredeemed CLIENT grant.
        ticket_body["channelToken"]={{"type",1},{"code",game_auth_code}};
        // Select the companion transport's ticket platform (H5=7, APK=1).
        // This is not the independently verified passport X-DeviceType.
        ticket_body["type"]=0;ticket_body["platform"]=kCloudTicketPlatform;
    }
    const auto ticket=request("https://u8.hypergryph.com/u8/user/auth/v2/token_by_channel_token",ticket_body,true);
    const auto u8_token=read_token(ticket);
    if(!u8_token) return result;
    if(stopped()) {result.error={-2,"cancelled"};return result;}
    result.credentials=CloudCredentials{*cloud_token,*u8_token,std::move(game_auth_code)};
    result.error={};
    return result;
}

U8TokenResult FetchU8Token(const std::string& hg_token, const std::string& uid,
                          const std::atomic_bool* cancelled) {
    U8TokenResult result;
    if (hg_token.empty() || uid.empty()) {
        result.error = {-9, "u8 arguments missing"};
        return result;
    }
    // 1) OAuth grant for the binding service (hg-account-web-sdk 3.5.0).
    const nlohmann::json grant_body = {
        {"token", hg_token},
        {"appCode", std::string(kBindingServiceAppCode)},
        {"type", 1},
    };
    long http = 0;
    const auto grant_reply = PostAccount(
        std::string(kPassportHostUrlDetail) + "/user/oauth2/v2/grant", grant_body.dump(),
        http,cancelled);
    if (http < 200 || http >= 300) {
        result.error = {-20, "grant request failed"};
        result.error.http = http;
        return result;
    }
    const auto grant_token = ParseTokenEnvelope(grant_reply.body);
    if (!grant_token) {
        result.error = {-21, "grant response missing token"};
        result.error.http = http;
        return result;
    }
    if(cancelled && cancelled->load()) {result.error={-2,"cancelled"};return result;}
    // 2) u8 ticket for the login uid (same SDK, binding server).
    const nlohmann::json u8_body = {
        {"token", *grant_token},
        {"uid", uid},
    };
    http = 0;
    const auto u8_reply = PostAccount(
        std::string(kBindingHostUrlDetail) + "/account/binding/v1/u8_token_by_uid",
        u8_body.dump(), http,cancelled);
    if (http < 200 || http >= 300) {
        result.error = {-22, "u8 ticket request failed"};
        result.error.http = http;
        return result;
    }
    const auto u8_token = ParseTokenEnvelope(u8_reply.body);
    if (!u8_token) {
        result.error = {-23, "u8 ticket response missing token"};
        result.error.http = http;
        return result;
    }
    result.token = std::move(*u8_token);
    return result;
}

UidResult FetchBindingUid(const std::string& hg_token,const std::atomic_bool* cancelled) {
    UidResult result;
    if (hg_token.empty()) {
        result.error = {-9, "uid arguments missing"};
        return result;
    }
    // 1) OAuth grant for the binding service (same step as FetchU8Token).
    result.stage=UidResult::Stage::Grant;
    const nlohmann::json grant_body = {
        {"token", hg_token},
        {"appCode", std::string(kBindingServiceAppCode)},
        {"type", 1},
    };
    long http = 0;
    const auto grant_reply = PostAccount(
        std::string(kPassportHostUrlDetail) + "/user/oauth2/v2/grant", grant_body.dump(),
        http,cancelled);
    if (http < 200 || http >= 300) {
        result.error = {-20, "grant request failed"};
        result.error.http = http;
        return result;
    }
    const auto grant_token = ParseTokenEnvelope(grant_reply.body);
    if (!grant_token) {
        result.error = {-21, "grant response missing token"};
        const auto reply=account::Parse(grant_reply.body);
        if(reply.parsed && reply.status!=0) result.error.code=reply.status;
        result.error.http = http;
        return result;
    }
    // 2) Binding list for the Endfield app code; the entry carries the uid.
    result.stage=UidResult::Stage::BindingList;
    const std::string path = std::string("/account/binding/v1/binding_list?token=") +
                             account::QueryValue(*grant_token) + "&appCode=" + std::string(kEndfieldU8AppCode);
    http = 0;
    const auto list_reply = GetAccount(std::string(kBindingHostUrlDetail) + path, http,cancelled);
    if (http < 200 || http >= 300) {
        result.error = {-24, "binding list request failed"};
        result.error.http = http;
        return result;
    }
    const auto uid = FindBindingUid(list_reply.body);
    if (!uid) {
        result.error = {-25, "binding list response missing uid"};
        const auto reply=account::Parse(list_reply.body);
        if(reply.parsed && reply.status!=0) result.error.code=reply.status;
        result.error.http = http;
        return result;
    }
    result.uid = *uid;
    return result;
}

}  // namespace cloudgenshin::endfield::api

namespace cloudgenshin::endfield::login {

GameAuthorization AuthorizeWebGame(const std::string& passport_token,const std::string& device_token,
                                   const std::atomic_bool* cancelled) {
    if(cancelled && cancelled->load()) return {"",-2,0};
    if(passport_token.empty() || passport_token.size()>4096 || device_token.size()>8192)
        return {"",-9,0};
    // FetchCloudCredentials' legacy no-proof branch is an OTHER grant, not
    // the H5 CLIENT contract. Never silently hand that result to this bridge.
    if(device_token.empty()) return {"",-22,0};
    const auto tickets=api::FetchCloudCredentials(passport_token,cancelled,device_token);
    if(!tickets.credentials) return {"",tickets.error.code,tickets.error.http};
    if(cancelled && cancelled->load()) return {"",-2,0};
    const auto& credentials=*tickets.credentials;
    // Module 20208 gates checkAndUpdateToken on the user's already-accepted
    // U8 agreement. Query only: accepting a new/updated agreement is a human
    // action, never a background repair or an automatic POST update_agreement.
    try {
        opennow::HttpTransferControl control;
        control.cancelled=cancelled;control.timeout_ms=15000;control.max_body_bytes=65536;
        const auto agreement=opennow::HttpClient{}.Request("GET",
            "https://u8.hypergryph.com/u8/user/info/v2/agreement_version?token="+
                account::QueryValue(credentials.u8_token),std::string(account::kUserAgent),
            {"Accept: application/json","X-PackName: web.official.cloud",
             "Accept-Encoding: identity","Connection: close"},"","",control);
        if(cancelled && cancelled->load()) return {"",-2,agreement.status_code};
        if(agreement.status_code<200 || agreement.status_code>=300) return {"",-20,agreement.status_code};
        const auto state=account::Agreement(agreement.body);
        if(state==account::AgreementState::NeedsConsent) return {"",-23,agreement.status_code};
        if(state!=account::AgreementState::Accepted) {
            const auto reply=account::Parse(agreement.body);
            return {"",reply.parsed && reply.status!=0 ? reply.status : -12,agreement.status_code};
        }
    } catch(const opennow::HttpRequestError& error) {
        return {"",-1000-static_cast<long>(error.curl_code),error.http_status};
    } catch(const std::exception&) {return {"",-20,0};}
    // Official module 61522 ex() only reports success after et(token,u8).
    // This refresh does not dispatch a pod, requeue, buy time or overwrite
    // the saved account. Keep its raw CLIENT code separate from both tokens.
    const auto exchange=api::Exchange(credentials.hg_token,credentials.u8_token,cancelled);
    if(!exchange.session) return {"",exchange.error.code,exchange.error.http};
    if(cancelled && cancelled->load()) return {"",-2,exchange.error.http};
    return {credentials.game_auth_code,0,exchange.error.http};
}

} // namespace cloudgenshin::endfield::login
