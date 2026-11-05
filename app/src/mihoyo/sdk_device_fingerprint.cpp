#include "mihoyo/sdk_device_fingerprint.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/sdk_json.hpp"
#include "http_client.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin {
namespace {
using Json=nlohmann::json;
bool Text(std::string_view text,std::size_t limit) {
    return !text.empty() && text.size()<=limit &&
        std::all_of(text.begin(),text.end(),[](unsigned char c){return c>=0x20 && c<0x7f;});
}
std::string RandomText(std::size_t size,std::string_view alphabet) {
    std::array<unsigned char,16> bytes{};
#ifdef __SWITCH__
    randomGet(bytes.data(),bytes.size());
#else
    std::random_device random;for(auto& byte:bytes) byte=static_cast<unsigned char>(random());
#endif
    std::string result;for(std::size_t i=0;i<size;++i) result+=alphabet[bytes[i]%alphabet.size()];
    return result;
}
std::optional<Json> Data(const opennow::HttpResponse& response,const DeviceFingerprintProgress& progress) {
    if(response.aigis_header_present) {
        progress("SDK_DEVICE_FP_VERIFICATION_REQUIRED",0,response.status_code);return {};
    }
    if(response.status_code!=200 || response.body.size()>16384) {
        progress("SDK_DEVICE_FP_HTTP_FAILED",0,response.status_code);return {};
    }
    const auto json=sdk::ParseSdkJson(response.body);
    if(!json.is_object() || !json.contains("retcode") || !json["retcode"].is_number_integer() || json["retcode"]!=0 ||
       !json.contains("data") || !json["data"].is_object() ||
       !json["data"].contains("code") || !json["data"]["code"].is_number_integer() || json["data"]["code"]!=200) {
        // No server message or arbitrary error body is exposed.
        progress("SDK_DEVICE_FP_RESPONSE_REJECTED",0,response.status_code);return {};
    }
    return json["data"];
}
}
std::optional<std::string> FetchSdkDeviceFingerprint(const GameProfile& profile,
    const auth::WebDeviceIdentity& device,const std::atomic_bool& cancelled,const DeviceFingerprintProgress& progress) {
    if(cancelled) return {};
    const auto* official=FindGameProfile("hkrpg");
    if(!official || profile.id!="hkrpg" || profile.app_id!=8 || profile.game_biz!="hkrpg_cn" ||
       profile.sdk_base!=official->sdk_base || profile.cloud_base!=official->cloud_base ||
       !Text(device.device_id,128) || device.device_id.find_first_of(" ;\r\n")!=std::string::npos ||
       !Text(device.user_agent,512)) return {};
    // combo-web defaults ae.device_fp -> H().getDeviceFp(). Ct._init supplies
    // clientType to the FP SDK, so use its actual H5 PC value 22, not Android.
    // Fixed official hosts, no credentials/redirects or arbitrary targets.
    const std::vector<std::string> headers{"Accept: application/json","Content-Type: application/json"};
    const opennow::HttpTransferControl control{.cancelled=&cancelled,.max_body_bytes=16384,
        .follow_redirects=false,.timeout_ms=10000,.probe_verification_headers=true};
    progress("SDK_DEVICE_FP_FETCHING",0,0);
    try {
        progress("SDK_DEVICE_FP_STAGE",1,0);
        const auto config=opennow::HttpClient{}.Get(
            "https://public-data-api.mihoyo.com/device-fp/api/getExtList?platform=22",
            device.user_agent,headers,{},control);
        if(cancelled) return {};
        const auto data=Data(config,progress);
        if(!data || !data->contains("ext_list") || !(*data)["ext_list"].is_array() ||
           (*data)["ext_list"].size()>64) return {};
        // Only real native-client observations. No claimed Android/Windows,
        // browser canvas, WebGL identity or invented lie/automation flags.
        const Json known{{"userAgent",device.user_agent},{"browserScreenSize","921600"},
            {"isTouchSupported","1"},{"browserLanguage","zh-cn"},{"browserPlat","Horizon"},
            {"packageName","CloudGenshin-NX"},{"packageVersion","2.54.0"}};
        Json ext=Json::object();
        for(const auto& item:(*data)["ext_list"]) {
            if(!item.is_string() || !Text(item.get_ref<const std::string&>(),64)) return {};
            const auto& key=item.get_ref<const std::string&>();
            ext[key]=known.contains(key) ? known[key] : Json("unknown");
        }
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        // Official module 795 uses a random bootstrap device_fp in the request.
        // It is a request nonce, NEVER accepted as the game-facing fingerprint.
        const Json body{{"device_id",device.device_id},{"seed_id",RandomText(16,"0123456789abcdef")},
            {"seed_time",std::to_string(now)},{"platform","22"},
            {"device_fp",RandomText(10,"0123456789")},{"app_name","hkrpg_cn"},{"ext_fields",ext.dump()}};
        progress("SDK_DEVICE_FP_STAGE",2,0);
        const auto response=opennow::HttpClient{}.Post(
            "https://public-data-api.mihoyo.com/device-fp/api/getFp",device.user_agent,headers,body.dump(),{},control);
        if(cancelled) return {};
        const auto issued=Data(response,progress);
        if(!issued || !issued->contains("device_fp") || !(*issued)["device_fp"].is_string() ||
           !ValidSdkDeviceFingerprint((*issued)["device_fp"].get_ref<const std::string&>())) return {};
        progress("SDK_DEVICE_FP_SERVICE_READY",1,response.status_code);
        return (*issued)["device_fp"].get<std::string>();
    } catch(const opennow::HttpRequestError& error) {
        if(!cancelled) progress("SDK_DEVICE_FP_NETWORK_FAILED",static_cast<int>(error.curl_code),error.http_status);
    } catch(...) {if(!cancelled) progress("SDK_DEVICE_FP_RESPONSE_INVALID",0,0);}
    return {};
}
}
