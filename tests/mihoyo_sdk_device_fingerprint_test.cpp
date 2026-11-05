#include "mihoyo/sdk_device_fingerprint.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"
#include "mihoyo/sdk_json.hpp"
#include <cassert>

namespace {
using Json=nlohmann::json;
enum class Mode {Good,Http,Network,Verification,Cancel,InvalidFp,MissingFp,ConfigRejected,BadList,Oversize} mode;
unsigned calls=0;
std::atomic_bool cancelled{false};
std::vector<std::string> events;
const std::string issued="synthetic-service-fp";
const auto profile=cloudgenshin::FindGameProfile("hkrpg");
const auto device=cloudgenshin::NativeWebIdentity(*profile,"synthetic-device-id");
void Check(const std::string& url,const std::string& ua,const std::vector<std::string>& headers,
           const std::string& proxy,const opennow::HttpTransferControl& control) {
    assert(url.starts_with("https://public-data-api.mihoyo.com/device-fp/api/") && ua==device.user_agent && proxy.empty());
    assert(control.cancelled==&cancelled && !control.cookie_session && !control.follow_redirects &&
           control.timeout_ms==10000 && control.max_body_bytes==16384 && control.probe_verification_headers);
    for(const auto& header:headers) assert(!header.starts_with("Cookie:") && !header.starts_with("Authorization:") &&
        header.find("synthetic-device-id")==std::string::npos);
}
void Reset(Mode next) {mode=next;calls=0;cancelled=false;events.clear();}
}
namespace opennow {
HttpResponse HttpClient::Get(const std::string& url,const std::string& ua,const std::vector<std::string>& headers,
    const std::string& proxy,HttpTransferControl control) const {
    ++calls;Check(url,ua,headers,proxy,control);
    assert(calls==1 && url=="https://public-data-api.mihoyo.com/device-fp/api/getExtList?platform=22");
    if(mode==Mode::Network) throw HttpRequestError(CURLE_OPERATION_TIMEDOUT,0,"synthetic error");
    if(mode==Mode::Http) return {503,"private-error-text",false};
    if(mode==Mode::Verification) return {200,"{}",true};
    if(mode==Mode::Cancel) cancelled=true;
    Json ext=Json::array({"userAgent","browserScreenSize","browserPlat","packageVersion","webgl","hasLiedBrowser"});
    if(mode==Mode::BadList) ext.push_back("bad\r\nkey");
    const Json data{{"code",mode==Mode::ConfigRejected?403:200},{"ext_list",ext}};
    return {200,Json{{"retcode",0},{"data",data}}.dump(),false};
}
HttpResponse HttpClient::Post(const std::string& url,const std::string& ua,const std::vector<std::string>& headers,
    const std::string& body,const std::string& proxy,HttpTransferControl control) const {
    ++calls;Check(url,ua,headers,proxy,control);
    assert(calls==2 && url=="https://public-data-api.mihoyo.com/device-fp/api/getFp");
    const auto request=Json::parse(body);
    assert(request.size()==7 && request["device_id"]==device.device_id && request["platform"]=="22" && request["app_name"]=="hkrpg_cn");
    assert(request["seed_id"].get<std::string>().size()==16 && !request["seed_time"].get<std::string>().empty());
    const auto nonce=request["device_fp"].get<std::string>();
    assert(nonce.size()==10 && std::all_of(nonce.begin(),nonce.end(),[](char c){return c>='0' && c<='9';}));
    const auto ext=Json::parse(request["ext_fields"].get<std::string>());
    assert(ext["userAgent"]==device.user_agent && ext["browserPlat"]=="Horizon" && ext["browserScreenSize"]=="921600");
    assert(ext["packageVersion"]=="2.54.0" && ext["webgl"]=="unknown" && ext["hasLiedBrowser"]=="unknown");
    assert(!request.contains("account_id") && !request.contains("token") && !request.contains("stoken"));
    if(mode==Mode::Oversize) return {200,std::string(16385,'x'),false};
    Json data{{"code",200},{"device_fp",mode==Mode::InvalidFp?"bad\r\nvalue":issued}};
    if(mode==Mode::MissingFp) data.erase("device_fp");
    return {200,Json{{"retcode",0},{"data",data}}.dump(),false};
}
}
int main() {
    using namespace cloudgenshin;
    const DeviceFingerprintProgress progress=[](const std::string& event,int,long) {
        assert(event.find(issued)==event.npos && event.find(device.device_id)==event.npos && event.find("private-error-text")==event.npos);
        events.push_back(event);
    };
    Reset(Mode::Good);
    const auto result=FetchSdkDeviceFingerprint(*profile,device,cancelled,progress);
    assert(result && *result==issued && calls==2 && events.back()=="SDK_DEVICE_FP_SERVICE_READY");
    for(const auto test:{Mode::Http,Mode::Network,Mode::Verification,Mode::Cancel,Mode::InvalidFp,
                        Mode::MissingFp,Mode::ConfigRejected,Mode::BadList,Mode::Oversize}) {
        Reset(test);assert(!FetchSdkDeviceFingerprint(*profile,device,cancelled,progress));
        assert(calls<=2 && std::find(events.begin(),events.end(),"SDK_DEVICE_FP_SERVICE_READY")==events.end());
    }
    Reset(Mode::Good);assert(!FetchSdkDeviceFingerprint(DefaultGameProfile(),device,cancelled,progress) && calls==0);
    auto bad=device;bad.device_id="bad\r\nCookie: secret";
    assert(!FetchSdkDeviceFingerprint(*profile,bad,cancelled,progress) && calls==0);
    cancelled=true;assert(!FetchSdkDeviceFingerprint(*profile,device,cancelled,progress) && calls==0);
    assert(!ValidSdkDeviceFingerprint("1234567890") && !ValidSdkDeviceFingerprint("bad;injection=1"));
}
