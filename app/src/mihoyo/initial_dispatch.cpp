#include "mihoyo/initial_dispatch.hpp"
#include "mihoyo/production_profile.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <map>
#include <string_view>

namespace cloudgenshin::dispatch {
namespace {

bool SafeText(std::string_view text, std::size_t maximum) {
    if (text.empty() || text.size() > maximum) return false;
    for (const unsigned char c : text)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

std::optional<nlohmann::json> ParseRegions(const std::string& text) {
    if (text.size() > 2048) return std::nullopt;
    const auto regions = nlohmann::json::parse(text, nullptr, false);
    if (!regions.is_array() || regions.size() > 64) return std::nullopt;
    for (const auto& region : regions) {
        if (region.is_number_unsigned()) {
            if (region.get<std::uint64_t>() > 0xffffffffULL) return std::nullopt;
        } else if (region.is_number_integer()) {
            if (region.get<std::int64_t>() < 0 ||
                region.get<std::int64_t>() > 0xffffffffLL) return std::nullopt;
        } else if (region.is_string()) {
            if (!SafeText(region.get<std::string>(), 128)) return std::nullopt;
        } else {
            return std::nullopt;
        }
    }
    return regions;
}

} // namespace

std::optional<InitialDispatchRequest> BuildInitialDispatchRequest(
    const GameProfile& profile, const auth::ComboLoginFields& fields,
    const auth::WebDeviceIdentity& device, const InitialDispatchParams& params) {
    if (params.bit_rate < 128000 || params.bit_rate > 100000000 ||
        params.fps < 1 || params.fps > 120 ||
        !SafeText(params.node_id, 256) || !SafeText(params.resolution, 128) ||
        !SafeText(params.environment, 128) ||
        (params.net_state != 1 && params.net_state != 2 && params.net_state != 4) ||
        params.user_data_json.empty() || params.user_data_json.size() > 8192 ||
        (params.hint && !SafeText(*params.hint, 1024)))
        return std::nullopt;
    const auto user_data = nlohmann::json::parse(params.user_data_json, nullptr, false);
    if (!user_data.is_object()) return std::nullopt;
    const auto hint = params.hint ? nlohmann::json::parse(*params.hint, nullptr, false)
                                  : nlohmann::json(nullptr);
    if (params.hint && !hint.is_object()) return std::nullopt;
    const auto regions = ParseRegions(params.regions_json);
    if (!regions) return std::nullopt;
    const auto login = auth::BuildGamerLoginRequest(profile, fields, device);
    if (!login) return std::nullopt;

    // The web client signs only these fields, with regions serialized as a
    // string; hint/net_state/queue_type are not part of the signature.
    std::map<std::string, std::string> signed_fields = {
        {"bit_rate", std::to_string(params.bit_rate)},
        {"biz_key", std::string(profile.game_biz)},
        {"cmd_line", ""},
        {"codec_type", "1"},
        {"env", params.environment},
        {"ext_data", ""},
        {"fps", std::to_string(params.fps)},
        {"node_id", params.node_id},
        {"resolution", params.resolution},
        {"user_data", params.user_data_json},
    };
    if (!regions->empty()) signed_fields["regions"] = regions->dump();
    std::string to_sign;
    for (const auto& [key, value] : signed_fields) {
        if (!to_sign.empty()) to_sign += '&';
        to_sign += key + "=" + value;
    }
    const auto signature = auth::HmacSha256Hex(std::string(profile.dispatch_app_key), to_sign);
    if (!signature) return std::nullopt;

    nlohmann::json body = {
        {"bit_rate", params.bit_rate}, {"biz_key", std::string(profile.game_biz)},
        {"cmd_line", ""}, {"codec_type", 1}, {"env", params.environment},
        {"ext_data", ""}, {"fps", params.fps}, {"node_id", params.node_id},
        {"resolution", params.resolution}, {"user_data", params.user_data_json},
        {"regions", *regions}, {"hint", hint},
        {"net_state", params.net_state},
        {"queue_type", params.queue_type == QueueType::Coin ? "coin" : ""},
        {"queue_switch", params.queue_switch},
        {"using_new_cmd_line", true}, {"sign", *signature},
    };
    InitialDispatchRequest request{std::string(profile.cloud_base)+"/dispatcher/api/paasDispatch",
                                   login->user_agent, login->headers, body.dump()};
    request.headers.push_back("Content-Type: application/json");
    return request;
}

} // namespace cloudgenshin::dispatch
