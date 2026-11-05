#pragma once
#include "mihoyo/sdk_json.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

namespace cloudgenshin::auth {
// SDK-only login fields must survive QR -> vault -> game login. Never log this
// object: nested official data may contain account credentials as well.
inline constexpr std::size_t kMaximumComboSdkDataBytes = 4096;
using ComboSdkJson = nlohmann::json;

inline bool ValidComboSdkValue(const ComboSdkJson& value, unsigned depth = 0) {
    if (depth > 8 || value.is_discarded() || value.is_binary()) return false;
    if (value.is_number_float() && !std::isfinite(value.get<double>())) return false;
    if (value.is_structured()) {
        if (value.size() > 64) return false;
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (value.is_object()) {
                if (it.key().empty() || it.key().size() > 128) return false;
                for (const unsigned char c : it.key()) if (c < 0x20 || c == 0x7f) return false;
            }
            if (!ValidComboSdkValue(it.value(), depth + 1)) return false;
        }
    }
    return true;
}

inline bool ValidKnownComboSdkFields(const ComboSdkJson& data) {
    if (data.contains("heartbeat") && !data["heartbeat"].is_boolean()) return false;
    if (data.contains("account_type") &&
        (!data["account_type"].is_number_integer() || data["account_type"] < 0 ||
         data["account_type"] > 2147483647)) return false;
    if (data.contains("asterisk_name") &&
        (!data["asterisk_name"].is_string() || data["asterisk_name"].get_ref<const std::string&>().size() > 256))
        return false;
    if (data.contains("combo_id")) {
        const auto& id = data["combo_id"];
        if (id.is_string()) {
            const auto& text = id.get_ref<const std::string&>();
            if (text.empty() || text.size() > 20 ||
                !std::all_of(text.begin(), text.end(), [](char c) {return c >= '0' && c <= '9';})) return false;
        } else if (!id.is_number_integer() || id < 0) return false;
    }
    return true;
}

inline bool ReservedComboSdkField(std::string_view key) {
    // Identity/device secrets come only from the separately validated context.
    return key == "device_id" || key == "open_id" || key == "combo_token" ||
           key == "channel_token" || key == "app_id" || key == "channel_id";
}

// Old vaults kept the verified CN first-party Combo identity but dropped the
// SDK account namespace. The official account SDK uses type 1 for miHoYo
// accounts. Restore it only for the positively bound, numeric hk4e/channel-1
// identity; never overwrite an upstream type or extrapolate to another game.
// A same-vault yuzu A/B test reproduced 35-4301 without this field and entered
// the live world with this single field restored (2026-10-21).
inline bool ResolveLegacyGenshinAccountType(ComboSdkJson& data,std::string_view app,
    std::string_view channel,std::string_view open,std::string_view account) {
    constexpr std::string_view maximum="9223372036854775807";
    if(!data.is_object() || data.contains("account_type") || app!="4" || channel!="1" ||
       open!=account || account.empty() || account.front()=='0' || account.size()>maximum.size() ||
       (account.size()==maximum.size() && account>maximum) ||
       !std::all_of(account.begin(),account.end(),[](char c){return c>='0' && c<='9';})) return false;
    data["account_type"]=1;
    return true;
}

inline std::optional<ComboSdkJson> DecodeComboSdkData(std::string_view encoded) {
    if (encoded.empty()) return ComboSdkJson::object(); // legacy login/vault
    if (encoded.size() > kMaximumComboSdkDataBytes) return std::nullopt;
    auto data = cloudgenshin::sdk::ParseSdkJson(encoded);
    if (!data.is_object() || !ValidComboSdkValue(data) || !ValidKnownComboSdkFields(data)) return std::nullopt;
    for (auto it = data.begin(); it != data.end(); ++it)
        if (ReservedComboSdkField(it.key())) return std::nullopt;
    return data;
}

inline bool H5ComboHostField(std::string_view key) {
    return key=="heartbeat" || key=="fatigue_remind" || key=="data";
}
inline unsigned H5ComboAdditionalRootFields(const ComboSdkJson& fields) {
    if(!fields.is_object()) return 0;
    unsigned count=0;
    for(auto it=fields.begin();it!=fields.end();++it)
        if(!ReservedComboSdkField(it.key()) && !H5ComboHostField(it.key()) &&
           it.key()!="combo_id" && it.key()!="account_type" && it.key()!="asterisk_name") ++count;
    return count;
}
inline std::optional<std::string> ExtractComboSdkData(const ComboSdkJson& fields,bool h5_result=false) {
    if (!fields.is_object()) return std::nullopt;
    ComboSdkJson data = ComboSdkJson::object();
    if(h5_result) {
        // hkrpg combo-web keeps ALL other root response fields, not only this
        // client's old four-field allowlist. Validated identities still come
        // from BridgeContext; host-only fields are not game-login fields.
        for(auto it=fields.begin();it!=fields.end();++it)
            if(!ReservedComboSdkField(it.key()) && !H5ComboHostField(it.key())) data[it.key()]=it.value();
    } else {
        for (const auto key : {"combo_id", "account_type", "heartbeat", "asterisk_name"})
            if (fields.contains(key)) data[key] = fields[key];
    }
    // Official _createComboLoginSuccResult expands JSON.parse(result.data).
    if (fields.contains("data") && !fields["data"].is_null()) {
        auto extra = fields["data"];
        if (extra.is_string()) {
            const auto text = extra.get<std::string>();
            if (text.size() > kMaximumComboSdkDataBytes) return std::nullopt;
            extra = text.empty() ? ComboSdkJson::object() : cloudgenshin::sdk::ParseSdkJson(text);
        }
        if (!extra.is_object() || !ValidComboSdkValue(extra)) return std::nullopt;
        for (auto it = extra.begin(); it != extra.end(); ++it)
            if (!ReservedComboSdkField(it.key())) data[it.key()] = it.value();
    }
    if (!ValidComboSdkValue(data) || !ValidKnownComboSdkFields(data)) return std::nullopt;
    auto encoded = data.dump();
    if (encoded.size() > kMaximumComboSdkDataBytes) return std::nullopt;
    return data.empty() ? std::string() : encoded;
}
} // namespace cloudgenshin::auth
