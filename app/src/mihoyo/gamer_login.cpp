#include "mihoyo/gamer_login.hpp"
#include "mihoyo/production_profile.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>

namespace cloudgenshin::auth {
namespace {

constexpr std::size_t kMaximumResponseBytes = 1024 * 1024;

bool SafeHeaderValue(std::string_view value) {
    return value.size() <= 4096 && value.find_first_of("\r\n") == std::string_view::npos;
}

} // namespace

std::optional<GamerLoginRequest> BuildGamerLoginRequest(
    const GameProfile& profile, const ComboLoginFields& fields,
    const WebDeviceIdentity& device) {
    const auto combo_header = BuildComboTokenHeader(
        fields, std::string(profile.combo_app_key), std::string(profile.game_biz));
    if (!combo_header || device.user_agent.empty() || device.client_type.empty() ||
        device.app_version.empty() || device.device_id.empty())
        return std::nullopt;
    // The official clients identify cloud-game calls with the app-upgrade app
    // id, not the Combo app id; profiles without one keep the Combo value.
    const std::string cloud_app_id = profile.cloud_app_id.empty()
        ? fields.app_id : std::string(profile.cloud_app_id);
    const std::array<std::string_view, 11> values = {
        device.user_agent, device.client_type, device.app_version,
        device.system_version, device.device_id, device.device_name,
        device.device_model, device.op_biz, device.language, device.cps,
        cloud_app_id,
    };
    for (const auto value : values)
        if (!SafeHeaderValue(value)) return std::nullopt;

    GamerLoginRequest request{std::string(profile.cloud_base) + "/gamer/api/login",
                              device.user_agent, {}};
    request.headers = {
        "x-rpc-client_type: " + device.client_type,
        "x-rpc-app_version: " + device.app_version,
        "x-rpc-sys_version: " + device.system_version,
        "x-rpc-channel: mihoyo",
        "x-rpc-device_id: " + device.device_id,
        "x-rpc-device_name: " + device.device_name,
        "x-rpc-device_model: " + device.device_model,
        "x-rpc-vendor_id: 2",
        "x-rpc-cg_game_biz: " + std::string(profile.game_biz),
        "x-rpc-op_biz: " + device.op_biz,
        "x-rpc-language: " + device.language,
        "x-rpc-cps: " + device.cps,
        "x-rpc-combo_token: " + *combo_header,
        "x-rpc-app_id: " + cloud_app_id,
    };
    return request;
}

GamerLoginResult ParseGamerLoginResponse(long http_status, const std::string& body) {
    if (http_status < 200 || http_status >= 300)
        return {GamerLoginStatus::HttpError, http_status};
    if (body.empty() || body.size() > kMaximumResponseBytes)
        return {GamerLoginStatus::InvalidResponse, http_status};
    const auto json = nlohmann::json::parse(body, nullptr, false);
    if (!json.is_object() || !json.contains("retcode") ||
        !json["retcode"].is_number_integer())
        return {GamerLoginStatus::InvalidResponse, http_status};
    const auto& code = json["retcode"];
    int retcode = 0;
    if (code.is_number_unsigned()) {
        const auto value = code.get<std::uint64_t>();
        if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return {GamerLoginStatus::InvalidResponse, http_status};
        retcode = static_cast<int>(value);
    } else {
        const auto value = code.get<std::int64_t>();
        if (value < std::numeric_limits<int>::min() ||
            value > std::numeric_limits<int>::max())
            return {GamerLoginStatus::InvalidResponse, http_status};
        retcode = static_cast<int>(value);
    }
    return {retcode == 0 ? GamerLoginStatus::Success : GamerLoginStatus::Rejected,
            http_status, retcode};
}

} // namespace cloudgenshin::auth
