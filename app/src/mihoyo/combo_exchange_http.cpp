#include "mihoyo/combo_exchange.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"
#include "app_version.hpp"

namespace cloudgenshin::auth {
QrResult ExchangeAccountForCombo(const GameProfile& profile, const ComboExchangeRequest& request,
                                 const std::atomic_bool* cancelled) {
    const auto headers=BuildQrHeaders(profile,request.device);
    if (!headers || request.body.empty() || request.body.size()>16*1024) return {};
    try {
        const auto reply=opennow::HttpClient{}.Post(
            std::string(profile.sdk_base)+"/combo/granter/login/v2/login",
            std::string("CloudGenshin-NX/")+opennow::kCloudGenshinVersion,*headers,request.body,{},
            {.cancelled=cancelled,.max_body_bytes=64*1024,.follow_redirects=false,.probe_verification_headers=true});
        auto result=ParseComboExchangeResponse(profile,reply.status_code,reply.body);
        result.verification_header_present=reply.aigis_header_present;return result;
    } catch (...) {QrResult result;result.status=QrStatus::TransportError;return result;}
}
} // namespace cloudgenshin::auth
