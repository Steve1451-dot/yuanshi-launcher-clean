#pragma once

// Cloud-game backend client for 云·终末地 (prod).
//
// Mirrors the official web client requests documented in
// docs/cloudgenshin-endfield-findings.md (§3 headers/signature, §5 exchange,
// §6 session/queue, §14.5 dispatch fields):
//  - request bodies are serialized in the exact object-literal key order the
//    web client uses, because X-Signature covers the body bytes;
//  - headers: X-Signature (HMAC-SHA256), X-Timestamp, X-APP-CODE,
//    X-Client-App-Version / Platform / Device-Model / Os-Version, and
//    Authorization once a login session exists;
//  - the server clock offset is calibrated from response timestamps and a
//    single retry is made for transient and for timestamp errors (web tY).

#include <optional>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
#include "endfield/endfield_profile.hpp"

namespace cloudgenshin::endfield {

class EcdhP256;  // endfield/jwe.hpp

namespace api {

// Web response envelope: {data, timestamp, error:{code,msg}, isSuccess}.
struct Envelope {
    long http = 0;               // HTTP status of the reply (0 = no reply)
    bool parsed = false;         // body was a JSON object
    bool success = false;        // isSuccess == true
    long code = -1;              // error.code; -1 for transport/parse failures
    std::string message;         // error.msg (never credentials)
    std::string data_json;       // serialized data object ("" when absent)
    long long timestamp = 0;     // envelope timestamp, else data.timestamp
};

struct ApiError {
    long code = 0;               // 0 = success; -1 = transport/framework;
                                 // -9 token, -10 keygen, -12 response parse (local)
    long http = 0;               // HTTP status when a reply arrived (0 = none)
    std::string message;
    ApiError() = default;
    ApiError(long error_code, std::string error_message)
        : code(error_code), message(std::move(error_message)) {}
};

struct LoginSession {
    std::string login_session_id;
    std::string server_id;
    std::string tenant_key;
    std::string host_url;
    std::string game_id;
    std::string tenant_params;
    std::string tenant_jwe_alg_enc;  // diagnostics only, e.g. "alg=X enc=Y"
};

struct ExchangeResult {
    std::optional<LoginSession> session;
    ApiError error;
};

// Exchanges an app-granted hg_token and U8 ticket for a cloud login session. Generates the
// ephemeral ECDH key (epk_jwk) and decrypts vlink_tenant_data/tenant_params.
ExchangeResult Exchange(const std::string& hg_token, const std::string& u8_token,
                        const std::atomic_bool* cancelled=nullptr);

struct CloudCredentials {
    std::string hg_token; // Cloud-app OAuth grant, not the persisted passport token.
    std::string u8_token;
    std::string game_auth_code{}; // Web CLIENT grant shared with the game bridge; never logged.
};
struct CloudCredentialsResult {
    enum class Stage {Grant,GameTicket};
    std::optional<CloudCredentials> credentials;
    ApiError error;
    Stage stage=Stage::Grant;
};
// SDK User.grantApp(cloud, type=1) + U8.API.authByHG (channelToken type=0).
// Both accept the saved passport token. No role binding or remembered device
// cookie is required; normal server validation/captcha errors remain errors.
// With a remembered device proof, use the official web CLIENT +
// tokenByOauth2Code chain and retain its matching game authorization code.
CloudCredentialsResult FetchCloudCredentials(const std::string& passport_token,
                                             const std::atomic_bool* cancelled=nullptr,
                                             const std::string& device_token={});

struct U8TokenResult {
    std::optional<std::string> token;  // u8 ticket; never logged
    ApiError error;
};

// Documents the u8 ticket chain implemented in hg_api_http.cpp:
//   POST as.hypergryph.com/user/oauth2/v2/grant
//        {token: hg_token, appCode: binding, type: 1} -> grant token
//   POST binding-api-account-prod.hypergryph.com/account/binding/v1/
//        u8_token_by_uid {token: grant, uid: uid}      -> u8 token
// (hg-account-web-sdk 3.5.0, module account/U8). The uid comes from the
// binding list (FetchBindingUid) and is stored with the vault (v2).
U8TokenResult FetchU8Token(const std::string& hg_token, const std::string& uid,
                          const std::atomic_bool* cancelled=nullptr);

struct UidResult {
    enum class Stage {None,Grant,BindingList};
    std::string uid;  // account uid; never logged
    ApiError error;
    Stage stage=Stage::None;
};

// Resolves the passport uid that hg_token belongs to (hg-account-web-sdk
// 3.5.0, module account/binding):
//   POST as.hypergryph.com/user/oauth2/v2/grant
//        {token: hg_token, appCode: binding service, type: 1} -> grant token
//   GET  binding-api-account-prod.hypergryph.com/account/binding/v1/
//        binding_list?token=<grant>&appCode=<endfield u8 appCode>
//        -> list[appCode].bindingList[] (default, else last official, else
//           first entry)
// Optional role lookup. Cloud login itself does not depend on this binding.
UidResult FetchBindingUid(const std::string& hg_token,const std::atomic_bool* cancelled=nullptr);

// Instance key semantics of the official client (dO): tenant_params ends
// with the 6-character game id (leading zeros stripped) and starts with the
// instance id.
// Only for callers that actually have that literal SDK format. The cloud
// exchange's opaque tenant_params must not be parsed this way; normal cloud
// startup uses the tenant node list and passes cloud game_id unchanged.
struct InstanceKey {
    std::string instance_id;
    std::string game_id;
};
std::optional<InstanceKey> ParseInstanceKey(std::string_view tenant_params);

struct DeployNode {
    std::string node_id;
    std::string node_name;
    long long delay = 0;
    long long state = 0;
    long long normal_wait_time = 0;
    long long quick_wait_time = 0;
    long long trial_card_wait_time = 0;
    long long play_card_wait_time = 0;
};

struct NodeListResult {
    std::vector<DeployNode> nodes;  // ordered by delay
    ApiError error;                 // enrichment call error (deploy-only may still be ok)
    bool deploy_ok = false;         // the ISP host returned a deploy list
};

// Fetches the tenant's /ping/md5(tenantKey+"H5").html configuration from the
// official SDK CDN (with one backup host), then enriches the node list.
NodeListResult GetNodeList(const std::string& login_session_id,
                           const std::string& tenant_key,
                           const std::atomic_bool* cancelled=nullptr);

struct DispatchInputs {
    std::string tenant_key;
    std::string game_id;                   // Opaque cloud game_id, passed unchanged.
    std::string node_id;                    // empty = server default
    std::string resolution = "1280x720";
    int bit_rate = kDefaultBitRate;         // kbps, high tier upperLimit unless restored
    int fps = kDispatchFps;                 // 60 FPS unless restored
    int codec_type = 18;                    // H264
    std::string cmd_line;                   // empty = selected transport's bootstrap profile
};

struct QueueState {
    std::string session_id;
    long long status = 0;
    long long queuing_size = 0;
    long long queuing_seat_no = 0;
    long long estimated_wait_time = 0;
    long long user_level = 0;
    std::string sdk_msg;            // raw JSON text once a pod is assigned
    // The running game session is a separate id in the dispatch/poll payload;
    // the per-session cancel endpoint rejects the queue session id (23001).
    std::string game_session_id;
    // Official dispatch/poll status 2 authorizes starting the pod. A queued
    // response may already contain a placeholder sdk_msg such as "{}".
    bool pod_assigned() const noexcept { return status==2; }
};

struct DispatchResult {
    std::optional<QueueState> state;
    ApiError error;
};

struct PollResult {
    std::optional<QueueState> state;
    ApiError error;
};

DispatchResult Dispatch(const std::string& login_session_id, const DispatchInputs& inputs,
                        const std::atomic_bool* cancelled=nullptr);
PollResult Poll(const std::string& login_session_id, const std::string& session_id,
                const std::atomic_bool* cancelled=nullptr);
ApiError Cancel(const std::string& login_session_id, const std::string& session_id);

// --- request builders (bytes covered by X-Signature) ---------------------

std::string BuildExchangeBody(const std::string& hg_token, const std::string& u8_token,
                              const std::string& public_jwk);
std::string BuildDispatchBody(const DispatchInputs& inputs);
std::string BuildCancelBody(const std::string& session_id);
std::string BuildNodeListBody(const std::vector<DeployNode>& nodes);

// Official template with explicit game platform/web flags. The current H5
// transport uses (2, true); APK CloudQueueHelper uses (1, false).
std::string BuildCommandLine(const std::string& resolution, int cloud_platform, bool is_web);
// biz_data from the SDK's getBizData(); os is "WINDOWS" for this port so the
// pod uses Windows input/cursor semantics (findings §14.5).
std::string BuildBizData();

// --- parsing helpers (exposed for host tests) ----------------------------

Envelope ParseEnvelope(const std::string& body);
std::optional<LoginSession> ParseLoginSessionData(const std::string& data_json,
                                                  const EcdhP256* key,
                                                  std::string* decrypt_diagnostic);
std::optional<QueueState> ParseQueueState(const std::string& data_json);
// ISP host deploy list body: {nodeList:[{nodeId,nodeName,...}], ...}.
bool ParseDeployNodeList(const std::string& body, std::vector<DeployNode>* out);
// /api/v1/game/node_list data object: {node_list:[{node_id, ...}]}.
bool ParseNodeListData(const std::string& data_json, std::vector<DeployNode>* out);

}  // namespace api
}  // namespace cloudgenshin::endfield
