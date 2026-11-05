#include "mihoyo/passport_token_resume.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <mbedtls/md5.h>
#include <cassert>
#include <algorithm>

namespace {
using Json=nlohmann::json;
enum class Mode { Good, IntegerAid, WrongAccount, MissingMid, BadMid, WrongTokenType, ExchangeMismatch,
    MissingCookie, WebMismatch, ComboMismatch, Risk, Verification, Http, Network, Cancel, Malformed, Oversize } mode;
std::atomic_bool cancelled{false};
unsigned calls=0;
bool game_source=false;
bool retained_context=false;
std::vector<std::string> events;
const cloudgenshin::GameProfile* profile=cloudgenshin::FindGameProfile("hkrpg");
std::string Header(const std::vector<std::string>& headers,const std::string& prefix) {
    for(const auto& header:headers) if(header.starts_with(prefix)) return header.substr(prefix.size());
    return {};
}
std::string Digest(const std::string& input) {
    unsigned char hash[16]{};assert(mbedtls_md5(reinterpret_cast<const unsigned char*>(input.data()),input.size(),hash)==0);
    constexpr char hex[]="0123456789abcdef";std::string out;
    for(const auto byte:hash) {out+=hex[byte>>4];out+=hex[byte&15];}return out;
}
cloudgenshin::Authorization Saved() {
    return {{"8","1","123","synthetic-combo"},cloudgenshin::NativeWebIdentity(*profile,"12345678901234567890123456789012"),
        "123","synthetic-saved-stoken"};
}
void Reset(Mode value) {mode=value;calls=0;events.clear();cancelled=false;}
const cloudgenshin::Progress progress=[](const std::string& event,int,long) {events.push_back(event);};
}
namespace opennow {
HttpResponse HttpClient::Post(const std::string& url,const std::string&,const std::vector<std::string>& headers,
    const std::string& body,const std::string& proxy,HttpTransferControl control) const {
    ++calls;
    const auto stage=calls+(retained_context?1u:0u);
    assert(proxy.empty() && control.cancelled==&cancelled && !control.follow_redirects &&
        control.max_body_bytes==65536 && control.timeout_ms==15000 && control.probe_verification_headers && !control.cookie_session);
    if(stage==1) {
        if(game_source) {
            assert(url=="https://passport-api.mihoyo.com/account/ma-cn-session/app/getTokenByGameToken");
            assert(Json::parse(body)==Json({{"account_id",123},{"game_token","synthetic-saved-stoken"}}));
            assert(Header(headers,"Cookie: ").empty());
        } else {
            assert(url=="https://passport-api.mihoyo.com/account/ma-cn-session/app/getTokenBySToken" && body.empty());
            assert(Header(headers,"Cookie: ")=="stuid=123;stoken=synthetic-saved-stoken");
        }
    } else if(stage==2) {
        assert(url=="https://passport-api.mihoyo.com/account/ma-cn-session/app/exchange");
        const auto parsed=Json::parse(body);
        assert(parsed==Json({{"dst_token_type",4},{"src_token",{{"token","synthetic-upgraded-stoken"},{"token_type",1}}},{"mid","synthetic-mid"}}));
        assert(Header(headers,"Cookie: ").empty());
    } else {
        assert(Header(headers,"Cookie: ")=="account_id_v2=123;account_mid_v2=synthetic-mid;cookie_token_v2=synthetic-cookie");
        if(stage==3) assert(url=="https://passport-api.mihoyo.com/account/ma-cn-session/web/webVerifyForGame" && body.empty());
        if(stage==4) assert(url=="https://hkrpg-sdk.mihoyo.com/hkrpg_cn/combo/granter/login/webLogin" &&
            Json::parse(body)==Json({{"app_id",8},{"channel_id",1}}));
    }
    if(stage<=2) {
        assert(Header(headers,"x-rpc-app_id: ")=="c90mr1bwo2rk" && Header(headers,"x-rpc-client_type: ")=="2");
        const auto ds=Header(headers,"DS: ");const auto comma1=ds.find(','),comma2=ds.find(',',comma1+1);
        assert(comma1!=std::string::npos && comma2!=std::string::npos && comma2-comma1==7);
        const auto expected=Digest("salt=JwYDpKvLj6MrMqqYU6jTKF17KNO2PXoS&t="+ds.substr(0,comma1)+
            "&r="+ds.substr(comma1+1,6)+"&b="+(stage==1 && !game_source?"{}":body)+"&q=");
        assert(ds.substr(comma2+1)==expected);
    } else assert(Header(headers,"DS: ").empty());
    if(mode==Mode::Http) return {503,"synthetic private response"};
    if(mode==Mode::Network) throw HttpRequestError(CURLE_OPERATION_TIMEDOUT,0,"synthetic secret must not escape");
    if(mode==Mode::Risk) return {200,R"({"retcode":-3506,"message":"signature invalid; synthetic private token must not escape"})"};
    if(mode==Mode::Verification) return {200,"{}",true};
    if(mode==Mode::Malformed) return {200,"{"};
    if(mode==Mode::Oversize) return {200,std::string(65537,'x')};
    if(mode==Mode::Cancel) cancelled=true;
    Json data;
    if(stage==1) {
        data={{"token",{{"token","synthetic-upgraded-stoken"},{"token_type",mode==Mode::WrongTokenType?2:1}}},
            {"user_info",{{"aid",mode==Mode::WrongAccount?"456":"123"},{"mid","synthetic-mid"}}}};
        if(mode==Mode::IntegerAid) data["user_info"]["aid"]=123;
        if(mode==Mode::MissingMid) data["user_info"].erase("mid");
        if(mode==Mode::BadMid) data["user_info"]["mid"]="private;injection=yes";
    } else if(stage==2) {
        data={{"token",{{"token","synthetic-cookie"},{"token_type",4}}}};
        if(mode==Mode::MissingCookie) data["token"].erase("token");
        if(mode==Mode::ExchangeMismatch) data["user_info"]={{"aid","456"},{"mid","synthetic-mid"}};
    } else if(stage==3) {
        data={{"token",{{"token","synthetic-web-stoken"},{"token_type",1}}},
            {"user_info",{{"aid",mode==Mode::WebMismatch?"456":"123"},{"mid","synthetic-mid"}}},{"need_realperson",false}};
    } else {
        assert(stage==4);
        data={{"open_id",mode==Mode::ComboMismatch?"456":"123"},{"combo_token","synthetic-web-combo"},
            {"account_type",1},{"combo_id","123456"},{"data",R"({"uid":"123"})"}};
    }
    return {200,Json({{"retcode",0},{"data",data}}).dump()};
}
}
int main() {
    using namespace cloudgenshin;
    for(const auto game_token:{false,true}) {
    game_source=game_token;
    const auto source=game_token ? PassportResumeSource::GameToken : PassportResumeSource::SToken;
    for(const auto test:{Mode::Good,Mode::IntegerAid}) {
        Reset(test);const auto saved=Saved();const auto result=ResumePassportFromAccount(*profile,saved,cancelled,progress,source);
        assert(result && calls==4 && result->account_id==saved.account_id && result->combo.open_id==saved.combo.open_id);
        assert(result->channel_token=="synthetic-web-stoken" && result->combo.combo_token=="synthetic-web-combo");
        assert(saved.channel_token=="synthetic-saved-stoken" && saved.combo.combo_token=="synthetic-combo");
        assert(events.back()=="GAME_WEB_RESUME_OK");
    }
    for(const auto test:{Mode::WrongAccount,Mode::MissingMid,Mode::BadMid,Mode::WrongTokenType,Mode::Risk,
        Mode::Verification,Mode::Http,Mode::Network,Mode::Cancel,Mode::Malformed,Mode::Oversize,
        Mode::ExchangeMismatch,Mode::MissingCookie,Mode::WebMismatch,Mode::ComboMismatch}) {
        Reset(test);const auto saved=Saved();assert(!ResumePassportFromAccount(*profile,saved,cancelled,progress,source));
        assert(calls==(test==Mode::ComboMismatch?4u:test==Mode::WebMismatch?3u:
            test==Mode::ExchangeMismatch || test==Mode::MissingCookie?2u:1u));
        for(const auto& event:events) assert(event.starts_with("GAME_WEB_RESUME_") && event.find("synthetic")==event.npos);
        assert(saved.channel_token=="synthetic-saved-stoken");
        if(test==Mode::Risk) assert(std::count(events.begin(),events.end(),"GAME_WEB_RESUME_REJECTION_HINT_MASK")==1);
    }
    }
    game_source=false;
    retained_context=true;Reset(Mode::Good);
    auto web_saved=Saved();web_saved.passport_mid="synthetic-mid";web_saved.channel_token="synthetic-upgraded-stoken";
    const auto web_result=ResumePassportFromAccount(*profile,web_saved,cancelled,progress);
    assert(web_result && calls==3 && web_result->passport_mid==web_saved.passport_mid);
    assert(web_result->channel_token=="synthetic-web-stoken");
    retained_context=false;
    Reset(Mode::Good);web_saved.passport_mid="bad;cookie=1";
    assert(!ResumePassportFromAccount(*profile,web_saved,cancelled,progress) && calls==0);
    for(const auto token:{"synthetic;injected=1","bad\r\nCookie: x","","bad token"}) {
        Reset(Mode::Good);auto saved=Saved();saved.channel_token=token;
        assert(!ResumePassportFromAccount(*profile,saved,cancelled,progress) && calls==0);
    }
    Reset(Mode::Good);auto wrong=*profile;wrong.sdk_base="https://example.invalid/";
    assert(!ResumePassportFromAccount(wrong,Saved(),cancelled,progress) && calls==0);
    assert(!ResumePassportFromAccount(DefaultGameProfile(),Saved(),cancelled,progress) && calls==0);
    auto unsafe=Saved();unsafe.device.user_agent="bad\r\nInjected: yes";
    assert(!ResumePassportFromAccount(*profile,unsafe,cancelled,progress) && calls==0);
    auto too_large=Saved();too_large.account_id=too_large.combo.open_id="2147483648";
    assert(!ResumePassportFromAccount(*profile,too_large,cancelled,progress,PassportResumeSource::GameToken) && calls==0);
    cancelled=true;assert(!ResumePassportFromAccount(*profile,Saved(),cancelled,progress) && calls==0);
}
