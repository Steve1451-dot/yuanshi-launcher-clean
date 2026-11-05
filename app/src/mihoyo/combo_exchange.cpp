#include "mihoyo/combo_exchange.hpp"
#include "mihoyo/production_profile.hpp"
#include <borealis/extern/nlohmann/json.hpp>

namespace cloudgenshin::auth {
std::optional<ComboExchangeRequest> BuildComboExchangeRequest(
    const GameProfile& profile, const AccountLoginFields& account, const std::string& device) {
    using Json = nlohmann::json;
    if (!BuildQrFetchBody(profile, device) || account.token.empty() || account.token.size()>8192 ||
        account.uid.empty() || account.uid.size()>19 || account.uid.front()=='0' ||
        profile.combo_app_key.empty()) return std::nullopt;
    for (const char c:account.uid) if (c<'0' || c>'9') return std::nullopt;
    if (account.uid.size()==19 && account.uid>"9223372036854775807") return std::nullopt;
    for (const unsigned char c:account.token) if (c<0x20 || c==0x7f) return std::nullopt;
    const auto data=Json{{"uid",account.uid},{"token",account.token},{"guest",false}}.dump();
    // PlatformHandler.justLoginVerify: sorted app_id/channel_id/data/device.
    const auto sign=HmacSha256Hex(std::string(profile.combo_app_key),
        "app_id=" + AppIdText(profile) + "&channel_id=1&data=" + data + "&device=" + device);
    if (!sign) return std::nullopt;
    return ComboExchangeRequest{Json{{"app_id",profile.app_id},{"channel_id",1},{"data",data},
        {"device",device},{"sign",*sign}}.dump(),device};
}

QrResult ParseComboExchangeResponse(const GameProfile& profile, long http_status,
                                    const std::string& body) {
    using Json = nlohmann::json;
    // Reuse bounded envelope validation, including retcode/risk classification.
    auto result=ParseQrQueryResponse(profile,http_status,body);
    if (result.retcode!=0 || result.status==QrStatus::HttpError || body.size()>64*1024) return result;
    const auto json=Json::parse(body,nullptr,false);
    if (!json.is_object() || !json.contains("retcode") || json["retcode"]!=0 ||
        !json.contains("data") || !json["data"].is_object()) return result;
    auto fields=json["data"];
    fields["app_id"]=profile.app_id;fields["channel_id"]=1;
    return ParseQrQueryResponse(profile,http_status,Json{{"retcode",0},{"data",{{"stat","Confirmed"},
        {"payload",{{"proto","Combo"},{"raw",fields.dump()}}}}}}.dump());
}
} // namespace cloudgenshin::auth
