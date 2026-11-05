#include "endfield/hg_login.hpp"
#include "endfield/account_policy.hpp"

#include "http_client.hpp"
#include <borealis/extern/nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace cloudgenshin::endfield::login {
namespace {

// Plain JSON over TLS with the account SDK's device header shape. QR and
// subsequent grant/binding requests share one app-generated client identity.
// Application identity is the official appCode in the body.
constexpr std::string_view kUserAgent = account::kUserAgent;
constexpr std::size_t kMaxBodyBytes = 64U << 10U;
constexpr long kTimeoutMs = 15000;

HttpResult Perform(const std::string& method, const std::string& path,
                   const std::string& body, const std::atomic_bool* cancelled) {
    opennow::HttpClient client;
    opennow::HttpTransferControl control;
    control.cancelled = cancelled;
    control.timeout_ms = kTimeoutMs;
    control.max_body_bytes = kMaxBodyBytes;

    auto headers=account::SessionHeaders(!body.empty());
    headers.emplace_back("Accept-Encoding: identity");
    headers.emplace_back("Connection: close");

    const auto response = client.Request(method, std::string(kPassportHost) + path,
                                         std::string(kUserAgent), headers, body, "", control);
    HttpResult result;
    result.http_status = response.status_code;
    result.body = response.body;
    return result;
}

}  // namespace

HttpResult CreateScanSession(const std::atomic_bool* cancelled) {
    return Perform("POST", "/general/v1/gen_scan/login", BuildGenScanLoginBody(), cancelled);
}

HttpResult PollScanStatus(std::string_view scan_id, const std::atomic_bool* cancelled) {
    return Perform("GET", BuildScanStatusPath(scan_id), "", cancelled);
}

HttpResult ExchangeScanCode(std::string_view scan_code, const std::atomic_bool* cancelled) {
    return Perform("POST", "/user/auth/v1/token_by_scan_code",
                   BuildTokenByScanCodeBody(scan_code), cancelled);
}

GameAuthorization AuthorizeGame(const std::string& passport_token,const std::string& device_token,
                                const std::atomic_bool* cancelled) {
    GameAuthorization result;
    if(passport_token.empty() || passport_token.size()>4096 || device_token.size()>8192) {
        result.status=-9;return result;
    }
    if(cancelled && cancelled->load()) {result.status=-2;return result;}
    nlohmann::json body={{"token",passport_token},{"appCode",kPassportAppCode},{"type",0}};
    if(!device_token.empty()) body["deviceToken"]=device_token;
    try {
        const auto reply=Perform("POST","/user/oauth2/v2/grant",body.dump(),cancelled);
        result.http=reply.http_status;
        const auto envelope=ParseEnvelope(reply.body);
        result.status=envelope.parsed ? envelope.status : -12;
        if(result.http<200 || result.http>=300) {result.status=-20;return result;}
        if(result.status!=0) return result; // 109 requires official device verification.
        const auto data=nlohmann::json::parse(envelope.data_json,nullptr,false);
        if(data.is_object()) {
            const auto code=data.find("code");
            if(code!=data.end() && code->is_string() && !code->get_ref<const std::string&>().empty() &&
               code->get_ref<const std::string&>().size()<=4096)
                result.code=code->get<std::string>();
        }
        if(result.code.empty()) result.status=-21;
    } catch(const opennow::HttpRequestError& error) {
        result.http=error.http_status;result.status=-1000-static_cast<long>(error.curl_code);
    } catch(const std::exception&) {result.status=-20;}
    return result;
}

}  // namespace cloudgenshin::endfield::login
