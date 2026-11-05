#include "mihoyo/qr_login.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"
#include "app_version.hpp"

namespace cloudgenshin::auth {
namespace {
QrResult Request(bool fetch, const GameProfile& profile, const std::string& device,
                 const std::string& ticket, const std::atomic_bool* cancelled) {
    const auto body = fetch ? BuildQrFetchBody(profile, device)
                            : BuildQrQueryBody(profile, device, ticket);
    const auto headers = BuildQrHeaders(profile, device);
    if (!body || !headers) return {};
    try {
        const auto reply = opennow::HttpClient{}.Post(
            std::string(profile.sdk_base) + "/combo/panda/qrcode/" + (fetch ? "fetch" : "query"),
            std::string("CloudGenshin-NX/")+opennow::kCloudGenshinVersion,
            *headers, *body, {},
            {.cancelled = cancelled, .max_body_bytes = 64 * 1024, .follow_redirects = false,
             .probe_verification_headers = true});
        auto result=fetch ? ParseQrFetchResponse(profile, reply.status_code, reply.body)
                          : ParseQrQueryResponse(profile, reply.status_code, reply.body);
        result.verification_header_present=reply.aigis_header_present;
        return result;
    } catch (...) { QrResult result; result.status = QrStatus::TransportError; return result; }
}
}

QrResult FetchQr(const GameProfile& profile, const std::string& device,
                 const std::atomic_bool* cancelled) {
    return Request(true, profile, device, {}, cancelled);
}
QrResult QueryQr(const GameProfile& profile, const std::string& device, const std::string& ticket,
                 const std::atomic_bool* cancelled) {
    return Request(false, profile, device, ticket, cancelled);
}
} // namespace cloudgenshin::auth
