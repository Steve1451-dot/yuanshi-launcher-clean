#pragma once

#include <borealis/extern/nlohmann/json.hpp>
#include <limits>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace cloudgenshin::endfield::account {
inline constexpr std::string_view kUserAgent=
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/124.0.0.0 Safari/537.36";
std::vector<std::string> SessionHeaders(bool json_body);
const std::string& SessionDeviceId(); // App-generated identity; never log it or use a hardware serial.
// hg-account-web-sdk 3.5.0 module 7142: these headers accompany both
// passport and binding requests. This is an app-generated client id, never a
// hardware serial or an account identifier. Do not export headers or URLs.
inline std::vector<std::string> Headers(std::string_view device_id,bool json_body) {
    // Match the configured desktop Chrome compatibility UA in hg_api_http.
    std::vector<std::string> headers={"Accept: application/json","X-DeviceModel: Chrome",
        "X-DeviceType: 7","X-OSVer: Windows","X-DeviceId: "+std::string(device_id),
        "X-Captcha-Version: 4.0"};
    if(json_body) headers.emplace_back("Content-Type: application/json");
    return headers;
}

inline std::string QueryValue(std::string_view input) {
    constexpr char hex[]="0123456789ABCDEF";
    std::string result;
    for(const unsigned char ch:input) {
        if((ch>='a' && ch<='z') || (ch>='A' && ch<='Z') || (ch>='0' && ch<='9') ||
           ch=='-' || ch=='_' || ch=='.' || ch=='~') result+=static_cast<char>(ch);
        else {result+='%';result+=hex[ch>>4];result+=hex[ch&15];}
    }
    return result;
}

struct Reply {
    bool parsed=false;
    long status=-1;
    nlohmann::json data;
};
inline Reply Parse(std::string_view body) {
    Reply reply;
    const auto root=nlohmann::json::parse(body,nullptr,false);
    if(!root.is_object()) return reply;
    auto field=root.find("status");
    if(field==root.end()) field=root.find("code");
    if(field==root.end() || !field->is_number_integer()) return reply;
    if(field->is_number_unsigned()) {
        const auto value=field->get<std::uint64_t>();
        if(value>static_cast<std::uint64_t>(std::numeric_limits<long>::max())) return reply;
        reply.status=static_cast<long>(value);
    } else {
        const auto value=field->get<std::int64_t>();
        if(value<std::numeric_limits<long>::min() || value>std::numeric_limits<long>::max()) return reply;
        reply.status=static_cast<long>(value);
    }
    reply.parsed=true;
    const auto data=root.find("data");
    if(data!=root.end()) reply.data=data->is_string()
        ? nlohmann::json::parse(data->get<std::string>(),nullptr,false) : *data;
    return reply;
}
inline std::optional<std::string> Token(std::string_view body) {
    const auto reply=Parse(body);
    if(!reply.parsed || reply.status!=0 || !reply.data.is_object()) return std::nullopt;
    const auto token=reply.data.find("token");
    if(token==reply.data.end() || !token->is_string()) return std::nullopt;
    const auto value=token->get<std::string>();
    return value.empty() ? std::nullopt : std::optional<std::string>(value);
}
enum class AgreementState { Invalid, NeedsConsent, Accepted };
// Official module 20208 supports data-object and legacy top-level fields.
// This parser never accepts/updates an agreement or keeps its dynamic URLs.
inline AgreementState Agreement(std::string_view body) {
    const auto reply=Parse(body);
    if(!reply.parsed || reply.status!=0) return AgreementState::Invalid;
    const auto root=nlohmann::json::parse(body,nullptr,false);
    const auto& data=reply.data.is_object() ? reply.data : root;
    const auto flag=[&](const char* name)->std::optional<bool> {
        const auto value=data.find(name);
        if(value==data.end()) return std::nullopt;
        if(value->is_boolean()) return value->get<bool>();
        if(value->is_number_integer() && (*value==0 || *value==1)) return *value==1;
        return std::nullopt;
    };
    const auto authorized=flag("authorized"),latest=flag("isLatestUserAgreement");
    if(!authorized || !latest) return AgreementState::Invalid;
    return *authorized && *latest ? AgreementState::Accepted : AgreementState::NeedsConsent;
}
inline std::optional<std::string> BindingUid(std::string_view body,std::string_view app_code) {
    const auto reply=Parse(body);
    if(!reply.parsed || reply.status!=0 || !reply.data.is_object()) return std::nullopt;
    const auto list=reply.data.find("list");
    if(list==reply.data.end() || !list->is_array()) return std::nullopt;
    for(const auto& entry:*list) {
        if(!entry.is_object()) continue;
        const auto app=entry.find("appCode");
        if(app==entry.end() || !app->is_string() || app->get<std::string>()!=app_code) continue;
        const auto bindings=entry.find("bindingList");
        if(bindings==entry.end() || !bindings->is_array()) return std::nullopt;
        std::string official,first;
        for(const auto& binding:*bindings) {
            if(!binding.is_object()) continue;
            const auto uid=binding.find("uid");
            if(uid==binding.end()) continue;
            std::string value;
            if(uid->is_string()) value=uid->get<std::string>();
            else if(uid->is_number_integer() && (uid->is_number_unsigned() || uid->get<std::int64_t>()>=0)) value=uid->dump();
            if(value.empty()) continue;
            if(first.empty()) first=value;
            const auto flag=[&](const char* key) {
                const auto f=binding.find(key);return f!=binding.end() && f->is_boolean() && f->get<bool>();
            };
            if(flag("isDefault")) return value;
            // SDK getActiveRole: first default, otherwise the last official
            // entry encountered, otherwise the first role.
            if(flag("isOfficial")) official=value;
        }
        if(!official.empty()) return official;
        if(!first.empty()) return first;
        return std::nullopt;
    }
    return std::nullopt;
}
} // namespace cloudgenshin::endfield::account
