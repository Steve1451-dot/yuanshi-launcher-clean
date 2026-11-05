#include "mihoyo/qr_login.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/combo_login_data.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <map>
#include <string_view>

namespace cloudgenshin::auth {
namespace {
using Json = nlohmann::json;

bool Text(std::string_view value, std::size_t limit) {
    if (value.empty() || value.size() > limit) return false;
    for (const unsigned char c : value)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

bool OfficialNumericAccount(std::string_view value) {
    // Android SdkLoginManager uses the CN Combo open_id as the numeric aid
    // for its account whitelist. Keep that mapping scoped to this profile.
    constexpr std::string_view maximum = "9223372036854775807";
    if (value.empty() || value.front() == '0' || value.size() > maximum.size()) return false;
    for (const char c : value) if (c < '0' || c > '9') return false;
    return value.size() < maximum.size() || value <= maximum;
}

std::optional<std::string> Field(const Json& json, const char* key,
                               std::size_t limit, bool numeric = false) {
    if (!json.is_object() || !json.contains(key)) return std::nullopt;
    const auto& value = json[key];
    std::string text;
    if (value.is_string()) text = value.get<std::string>();
    else if (numeric && value.is_number_integer() && value >= 0) text = value.dump();
    else return std::nullopt;
    return Text(text, limit) ? std::optional<std::string>(text) : std::nullopt;
}

std::optional<Json> Envelope(long status, const std::string& body, QrResult& result) {
    result.http_status = status;
    if (status < 200 || status >= 300) {
        result.status = QrStatus::HttpError;
        return std::nullopt;
    }
    if (body.empty() || body.size() > 64 * 1024) return std::nullopt;
    const auto json = Json::parse(body, nullptr, false);
    if (!json.is_object() || !json.contains("retcode") ||
        !json["retcode"].is_number_integer()) return std::nullopt;
    const auto& code = json["retcode"];
    if (code.is_number_unsigned()) {
        if (code.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return std::nullopt;
    } else if (code.get<std::int64_t>() < std::numeric_limits<int>::min() ||
               code.get<std::int64_t>() > std::numeric_limits<int>::max()) return std::nullopt;
    result.retcode = json["retcode"].get<int>();
    if (result.retcode != 0) {
        result.status = QrStatus::Rejected;
        // Do not retain or log the server message. Only recognize a fixed
        // risk phrase; an unfamiliar -3503 remains an ordinary rejection.
        if (result.retcode == -3503 && json.contains("message") && json["message"].is_string() &&
            json["message"].get<std::string>().find("设备或网络环境存在风险") != std::string::npos)
            result.status = QrStatus::RiskRejected;
        if (result.retcode == -106 || result.retcode == -3501) result.status = QrStatus::Expired;
        if (result.retcode == -3505) result.status = QrStatus::Cancelled;
        if (result.retcode == -3101 || result.retcode == -3235) result.status = QrStatus::VerificationRequired;
        return std::nullopt;
    }
    if (!json.contains("data") || !json["data"].is_object()) return std::nullopt;
    return json["data"];
}

int Hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<std::string> Unescape(std::string_view encoded) {
    std::string decoded;
    for (std::size_t i = 0; i < encoded.size(); ++i) {
        if (encoded[i] == '%') {
            if (encoded.size() - i < 3 || Hex(encoded[i + 1]) < 0 || Hex(encoded[i + 2]) < 0)
                return std::nullopt;
            decoded += static_cast<char>((Hex(encoded[i + 1]) << 4) | Hex(encoded[i + 2]));
            i += 2;
        } else decoded += encoded[i];
    }
    return Text(decoded, 4096) ? std::optional<std::string>(decoded) : std::nullopt;
}

std::optional<std::string> TicketFromUrl(const GameProfile& profile, std::string_view url) {
    if (!Text(url, 4096) || !url.starts_with("https://") || url.find('#') != url.npos)
        return std::nullopt;
    const auto authority_end = url.find_first_of("/?", 8);
    if (authority_end == url.npos) return std::nullopt;
    const auto host = url.substr(8, authority_end - 8);
    if (host != "mihoyo.com" && !host.ends_with(".mihoyo.com")) return std::nullopt;
    for (const char c : host)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.'))
            return std::nullopt;
    auto question = url.find('?');
    if (question == url.npos) return std::nullopt;
    std::map<std::string, std::string> parameters;
    auto query = url.substr(question + 1);
    while (!query.empty()) {
        const auto end = query.find('&');
        const auto pair = query.substr(0, end);
        const auto equal = pair.find('=');
        if (equal == pair.npos) return std::nullopt;
        const auto key = Unescape(pair.substr(0, equal));
        const auto value = pair.size() == equal + 1
            ? std::optional<std::string>("") : Unescape(pair.substr(equal + 1));
        if (!key || !value || !parameters.emplace(*key, *value).second) return std::nullopt;
        if (end == query.npos) break;
        query.remove_prefix(end + 1);
    }
    if (!parameters.contains("ticket") || parameters["ticket"].empty() ||
        parameters["app_id"] != AppIdText(profile) ||
        parameters["biz_key"] != profile.game_biz) return std::nullopt;
    return parameters["ticket"];
}
} // namespace

std::optional<std::vector<std::string>> BuildQrHeaders(const GameProfile& profile,
                                                       const std::string& device) {
    if (!Text(device, 128)) return std::nullopt;
    return std::vector<std::string>{"Content-Type: application/json",
        "x-rpc-app_id: " + AppIdText(profile),
        "x-rpc-device_id: " + device, "x-rpc-game_biz: " + std::string(profile.game_biz)};
}

std::optional<std::string> BuildQrFetchBody(const GameProfile& profile,
                                            const std::string& device) {
    if (!Text(device, 128)) return std::nullopt;
    return Json{{"app_id", profile.app_id}, {"device", device}}.dump();
}

std::optional<std::string> BuildQrQueryBody(const GameProfile& profile,
                                          const std::string& device,
                                          const std::string& ticket) {
    if (!Text(device, 128) || !Text(ticket, 4096)) return std::nullopt;
    return Json{{"app_id", profile.app_id}, {"device", device}, {"ticket", ticket}}.dump();
}

QrResult ParseQrFetchResponse(const GameProfile& profile, long http_status,
                              const std::string& body) {
    QrResult result;
    const auto data = Envelope(http_status, body, result);
    if (!data) return result;
    const auto url = Field(*data, "url", 4096);
    if (!url) return result;
    const auto ticket = TicketFromUrl(profile, *url);
    if (!ticket) return result;
    result.status = QrStatus::Ready;
    result.challenge = QrChallenge{*url, *ticket};
    return result;
}

QrResult ParseQrQueryResponse(const GameProfile& profile, long http_status,
                              const std::string& body) {
    QrResult result;
    const auto data = Envelope(http_status, body, result);
    if (!data) return result;
    const auto state = Field(*data, "stat", 32);
    if (!state) return result;
    if (*state == "Init") result.status = QrStatus::Waiting;
    else if (*state == "Scanned") result.status = QrStatus::Scanned;
    else if (*state == "Confirmed") {
        if (!data->contains("payload") || !(*data)["payload"].is_object()) return result;
        const auto& payload = (*data)["payload"];
        // QRManager.login in the APK sends ElementId.Combo.name and
        // UserData.toDataJSON() as payload.raw on the confirming device.
        const auto protocol = Field(payload, "proto", 64);
        const auto raw = Field(payload, "raw", 32 * 1024);
        if (!protocol || !raw) return result;
        const auto fields = Json::parse(*raw, nullptr, false);
        if (*protocol == "Account") {
            const auto uid = Field(fields, "uid", 128, true);
            const auto token = Field(fields, "token", 8192);
            if (!uid || !OfficialNumericAccount(*uid) || !token) return result;
            result.account_authorization = AccountLoginFields{*uid, *token};
            result.account_id = *uid;
            result.status = QrStatus::Confirmed;
            return result;
        }
        if (*protocol != "Combo") { result.status = QrStatus::UnsupportedProtocol; return result; }
        const auto app = Field(fields, "app_id", 32, true);
        const auto channel = Field(fields, "channel_id", 32, true);
        const auto open = Field(fields, "open_id", 1024);
        const auto token = Field(fields, "combo_token", 8192);
        if (!app || *app != AppIdText(profile) || !channel || !open || !token) return result;
        const bool h5_result=profile.id=="hkrpg";
        const auto sdk_data = ExtractComboSdkData(fields,h5_result);
        if (!sdk_data) return result;
        result.authorization = ComboLoginFields{*app, *channel, *open, *token, *sdk_data};
        if(h5_result) result.sdk_h5_additional_root_fields=H5ComboAdditionalRootFields(fields);
        const auto aid = Field(fields, "aid", 128, true);
        const auto uid = Field(fields, "uid", 128, true);
        if (aid && uid && *aid != *uid) return QrResult{};
        if (aid) result.account_id = *aid;
        else if (uid) result.account_id = *uid;
        else if (*channel == "1" && OfficialNumericAccount(*open)) result.account_id = *open;
        result.status = QrStatus::Confirmed;
    }
    return result;
}

} // namespace cloudgenshin::auth
