#pragma once
#include "mihoyo/sdk_json.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

namespace cloudgenshin::sdk {
struct ReportCode { const char* event; int value; };
// Fixed phrase presence, NOT a definitive failure classification. Input is
// only a named telemetry message field; no message, URL or credential escapes.
inline int ReportNetworkHintMask(std::string_view message) {
    if(message.size()>16384) return 0;
    std::string text(message);
    for(auto& c:text) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    const auto has=[&](std::string_view value){return text.find(value)!=text.npos;};
    int mask=0;
    if(has("resolve host") || has("getaddrinfo") || has("name resolution") || has("unknown host")) mask|=1;
    if(has("timeout") || has("timed out") || has("超时")) mask|=2;
    if(has("certificate") || has("ssl error") || has("tls error")) mask|=4;
    if(has("connection refused") || has("failed to connect") || has("couldn't connect")) mask|=8;
    if(has("http/1.1 403") || has("http/1.1 401") || has("http 403") || has("http 401")) mask|=16;
    if(has("http/1.1 500") || has("http/1.1 502") || has("http/1.1 503")) mask|=32;
    return mask;
}
// Only fixed-tag integer error fields leave this helper. No arbitrary key,
// event name, URL, message, token or JSON body can be returned to a log sink.
inline std::vector<ReportCode> InspectGameReport(const nlohmann::json& params) {
    using Json=nlohmann::json;
    std::vector<ReportCode> result;
    if(!params.is_object() || !params.contains("c_body")) return result;
    const auto emit=[&](const char* event,int value) {
        if(result.size()<12 && std::none_of(result.begin(),result.end(),[&](const ReportCode& item) {
            return std::string_view(item.event)==event && item.value==value;
        })) result.push_back({event,value});
    };
    const auto integer=[](const Json& item)->std::optional<int> {
        if(item.is_number_integer()) {
            if(item.is_number_unsigned()) {
                const auto value=item.get<std::uint64_t>();
                if(value>static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return {};
                return static_cast<int>(value);
            }
            const auto value=item.get<std::int64_t>();
            if(value<std::numeric_limits<int>::min() || value>std::numeric_limits<int>::max()) return {};
            return static_cast<int>(value);
        }
        if(!item.is_string()) return {};
        const auto& text=item.get_ref<const std::string&>();
        int value=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),value);
        if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size()) return {};
        return value;
    };
    unsigned visited=0;
    const auto walk=[&](auto&& self,const Json& item,unsigned depth)->void {
        if(depth>4 || ++visited>128 || result.size()>=12) return;
        if(item.is_string()) {
            const auto& text=item.get_ref<const std::string&>();
            if(text.size()>16384) return;
            if(text.find("35-4301")!=std::string::npos)
                emit("GAME_SDK_REPORT_LOGIN_35_4301",1);
            // Fixed-token presence checks only; the text itself is never copied.
            if(text.find("1001_2")!=std::string::npos)
                emit("GAME_SDK_REPORT_NET_1001_2",1);
            const auto nested=ParseSdkJson(text);
            if(nested.is_structured()) self(self,nested,depth+1);
            return;
        }
        if(item.is_array()) {
            for(const auto& value:item) {
                if(visited>=128 || result.size()>=12) break;
                self(self,value,depth+1);
            }
            return;
        }
        if(!item.is_object()) return;
        for(auto it=item.begin();it!=item.end() && visited<128 && result.size()<12;++it) {
            ++visited;
            const char* tag=nullptr;
            if(it.key()=="retcode" || it.key()=="ret_code" || it.key()=="retCode" || it.key()=="ret" || it.key()=="return_code")
                tag="GAME_SDK_REPORT_RETCODE";
            else if(it.key()=="error_code" || it.key()=="errorCode" || it.key()=="err_code" || it.key()=="code")
                tag="GAME_SDK_REPORT_ERROR_CODE";
            else if(it.key()=="http_code" || it.key()=="http_status" || it.key()=="status_code" || it.key()=="response_code")
                tag="GAME_SDK_REPORT_HTTP_CODE";
            if(tag) {if(const auto code=integer(it.value())) emit(tag,*code);}
            if((it.key()=="msg" || it.key()=="message" || it.key()=="error_message") && it.value().is_string())
                if(const int mask=ReportNetworkHintMask(it.value().get_ref<const std::string&>()))
                    emit("GAME_SDK_REPORT_NETWORK_HINT_MASK",mask);
            // Message text is never copied, only checked for the exact known
            // UI error; structured/nested JSON is inspected within the budget.
            if(it.value().is_structured() || it.value().is_string())
                self(self,it.value(),depth+1);
        }
    };
    const auto& body=params["c_body"];
    emit("GAME_SDK_REPORT_BODY_KIND",body.is_object()?1:body.is_string()?2:3);
    walk(walk,body,0);
    return result;
}
// Schema only, for the opt-in bounded protocol trace. Never returns values.
inline std::string GameReportSchema(const nlohmann::json& params) {
    if(!params.is_object() || !params.contains("c_body")) return "absent";
    auto body=params["c_body"];
    for(unsigned i=0;i<2 && body.is_string();++i) {
        const auto text=body.get<std::string>();if(text.size()>16384) return "oversize";
        auto decoded=ParseSdkJson(text);if(decoded.is_discarded()) return "text";
        body=std::move(decoded);
    }
    if(!body.is_object()) return body.is_array()?"array":"scalar";
    std::string result="object";unsigned count=0;
    for(auto it=body.begin();it!=body.end() && count<16;++it,++count) {
        const auto& key=it.key();
        if(key.empty() || key.size()>40 || !(key.front()=='_' || (key.front()>='a' && key.front()<='z') || (key.front()>='A' && key.front()<='Z')) ||
           !std::all_of(key.begin(),key.end(),[](unsigned char c){
               return c=='_' || (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9');
           })) continue;
        result+=' ';result+=key;result+=':';result+=it.value().type_name();
    }
    return result;
}
inline std::string GameCloudDataSchema(const nlohmann::json& value) {
    auto object=value;
    if(object.is_string()) {
        const auto& text=object.get_ref<const std::string&>();
        if(text.size()>131072) return "oversize";
        const auto parsed=ParseSdkJson(text);
        if(!parsed.is_object()) return "opaque-string";
        object=parsed;
    }
    if(!object.is_object()) return "non-object";
    std::string result="object";unsigned count=0;
    for(auto it=object.begin();it!=object.end() && count<24;++it,++count) {
        const auto& key=it.key();
        if(key.empty() || key.size()>64 || !std::all_of(key.begin(),key.end(),[](unsigned char c) {
            return c=='_' || (c>='a' && c<='z') || (c>='A' && c<='Z');
        })) {result+=" [dynamic-key]";continue;}
        result+=' ';result+=key;result+=':';result+=it.value().type_name();
    }
    return result;
}
} // namespace cloudgenshin::sdk
