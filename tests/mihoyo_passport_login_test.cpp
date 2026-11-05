#include "mihoyo/passport_login_flow.hpp"
#include "mihoyo/production_profile.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <cassert>

namespace {
using namespace cloudgenshin;
using namespace cloudgenshin::auth;
using Json=nlohmann::json;
constexpr char kTicket[]="11111111-2222-4333-8444-555555555555";
const PassportIdentity kIdentity{"aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee","0123456789",{}};
unsigned cloud_checks=0;
std::atomic_bool cancelled{false};
PassportReply Parse(PassportStep step,const Json& data) {
    return ParsePassportReply(step,DefaultGameProfile(),200,Json{{"retcode",0},{"data",data}}.dump());
}
Json CreateData(){return Json{{"url",std::string("https://user.mihoyo.com/login-platform/mobile.html?expire=123&tk=")+kTicket+"&token_types=4"},{"ticket",kTicket}};}
Json VerifyData(){return Json{{"user_info",{{"aid","123"},{"mid","synthetic-mid"}}},{"token",{{"token_type",1},{"token","synthetic-stoken"}}},{"need_realperson",false}};}
}
namespace cloudgenshin::auth {
GamerLoginResult CheckGamerLogin(const GameProfile&,const GamerLoginRequest&,
                               const std::atomic_bool*) {
    ++cloud_checks;return {GamerLoginStatus::Success,200,0};
}
}
int main() {
    const auto& profile=DefaultGameProfile();
    assert(SupportsPassportLogin(profile));
    // Both supported miHoYo cloud titles use the same passport flow with their own
    // Combo target; unknown hosts must still be refused.
    assert(SupportsPassportLogin(*FindGameProfile("hkrpg")));
    auto retired_profile=profile;retired_profile.game_biz="nap_cn";retired_profile.app_id=12;
    assert(!SupportsPassportLogin(retired_profile));
    auto bad_profile=profile;bad_profile.sdk_base="https://evil.invalid";assert(!SupportsPassportLogin(bad_profile));
    const auto headers=BuildPassportHeaders(profile,kIdentity,false);assert(headers);
    assert(std::find(headers->begin(),headers->end(),"x-rpc-app_id: c76ync6mutq8")!=headers->end());
    const auto star_headers=BuildPassportHeaders(*FindGameProfile("hkrpg"),kIdentity,false);assert(star_headers);
    assert(std::find(star_headers->begin(),star_headers->end(),"x-rpc-app_id: c90mr1bwo2rk")!=star_headers->end());
    for(const auto& header:*star_headers) assert(header.find("c76ync6mutq8")==header.npos && header.find("theme=ys")==header.npos);
    for(const auto& header:*headers) assert(!header.starts_with("Cookie:") && !header.starts_with("DS:"));
    const auto combo_headers=BuildPassportHeaders(profile,kIdentity,true);assert(combo_headers);
    assert(std::find(combo_headers->begin(),combo_headers->end(),"x-rpc-mdk_version: 2.55.0")!=combo_headers->end());
    auto bad_identity=kIdentity;bad_identity.device_id="bad\r\nCookie: secret";
    assert(!BuildPassportHeaders(profile,bad_identity,false));
    assert(BuildPassportBody(PassportStep::Create,profile,{})=="{}");
    assert(BuildPassportBody(PassportStep::Verify,profile,{})->empty());
    assert(Json::parse(*BuildPassportBody(PassportStep::Combo,profile,{}))["app_id"]==4);
    assert(!BuildPassportBody(PassportStep::Query,profile,"bad\nvalue"));
    const auto created=Parse(PassportStep::Create,CreateData());
    assert(created.status==QrStatus::Ready && created.challenge->ticket==kTicket);
    auto bad_create=CreateData();bad_create["url"]="https://evil.invalid/?tk="+std::string(kTicket);
    assert(Parse(PassportStep::Create,bad_create).status==QrStatus::InvalidResponse);
    bad_create=CreateData();bad_create["ticket"]="another-ticket";
    assert(Parse(PassportStep::Create,bad_create).status==QrStatus::InvalidResponse);
    bad_create=CreateData();bad_create["url"]=bad_create["url"].get<std::string>()+"&tk=other-ticket";
    assert(Parse(PassportStep::Create,bad_create).status==QrStatus::InvalidResponse);
    bad_create=CreateData();bad_create["url"]=bad_create["url"].get<std::string>()+"#different-route";
    assert(Parse(PassportStep::Create,bad_create).status==QrStatus::Ready);
    bad_create=CreateData();bad_create["url"]=bad_create["url"].get<std::string>()+"#/?tk=fragment-only";
    assert(Parse(PassportStep::Create,bad_create).challenge->ticket==kTicket);
    assert(Parse(PassportStep::Query,Json{{"status","Created"}}).status==QrStatus::Waiting);
    assert(Parse(PassportStep::Query,Json{{"status","Scanned"}}).status==QrStatus::Scanned);
    assert(Parse(PassportStep::Query,Json{{"status","Confirmed"}}).status==QrStatus::Confirmed);
    assert(Parse(PassportStep::Query,Json{{"status","Confirmed"},{"realname_info",{{"required",true}}}}).status==QrStatus::VerificationRequired);
    const auto verified=Parse(PassportStep::Verify,VerifyData());
    assert(verified.status==QrStatus::Confirmed && verified.channel_token=="synthetic-stoken" && verified.account_id=="123");
    assert(verified.passport_mid=="synthetic-mid");
    auto invalid_mid=VerifyData();invalid_mid["user_info"]["mid"]="bad;header=1";
    assert(Parse(PassportStep::Verify,invalid_mid).status==QrStatus::InvalidResponse);
    auto wrong_token=VerifyData();wrong_token["token"]["token_type"]=4;
    assert(Parse(PassportStep::Verify,wrong_token).status==QrStatus::InvalidResponse);
    wrong_token=VerifyData();wrong_token["token"]["token"]="unsafe\r\nvalue";
    assert(Parse(PassportStep::Verify,wrong_token).status==QrStatus::InvalidResponse);
    auto human=VerifyData();human["need_realperson"]=true;
    assert(Parse(PassportStep::Verify,human).status==QrStatus::VerificationRequired);
    assert(ParsePassportReply(PassportStep::Create,profile,403,"{}").status==QrStatus::HttpError);
    assert(ParsePassportReply(PassportStep::Create,profile,200,std::string(65537,'x')).status==QrStatus::InvalidResponse);
    assert(ParsePassportReply(PassportStep::Create,profile,200,R"({"retcode":18446744073709551615})").status==QrStatus::InvalidResponse);
    assert(ParsePassportReply(PassportStep::Create,profile,200,R"({"retcode":-2147483649})").status==QrStatus::InvalidResponse);
    assert(ParsePassportReply(PassportStep::Create,profile,200,R"({"retcode":0.0})").status==QrStatus::InvalidResponse);
    for(const auto [code,status]:{std::pair{-3501,QrStatus::Expired}, {-3503,QrStatus::Expired},
                                  {-3505,QrStatus::Cancelled},{-3506,QrStatus::RiskRejected}}) {
        assert(ParsePassportReply(PassportStep::Query,profile,200,Json{{"retcode",code}}.dump()).status==status);
    }
    std::vector<PassportStep> steps;
    std::vector<std::string> events;
    std::vector<std::string> qr;
    unsigned queries=0;
    const auto combo=Parse(PassportStep::Combo,Json{{"open_id","123"},{"combo_token","synthetic-combo"},
        {"combo_id",1234567890123456789LL},{"account_type",1},{"data",R"({"guest":false})"}});
    assert(combo.combo && combo.combo->sdk_login_data.find("guest")!=std::string::npos);
    PassportLoginTransport transport{
        [&](PassportStep step,const std::string& ticket){
            steps.push_back(step);
            if(step==PassportStep::Create) return created;
            if(step==PassportStep::Verify) return verified;
            if(step==PassportStep::Combo) return combo;
            assert(ticket==kTicket);++queries;
            return Parse(PassportStep::Query,Json{{"status",queries==1?"Scanned":"Confirmed"}});
        },[](std::chrono::milliseconds delay){assert(delay==std::chrono::seconds(2));return !cancelled.load();}};
    const auto progress=[&](const std::string& tag,int,long){events.push_back(tag);};
    const auto render=[&](const std::string& url){qr.push_back(url);};
    auto result=RunPassportLogin(profile,kIdentity,cancelled,render,progress,transport);
    assert(result && result->channel_token=="synthetic-stoken" && result->combo.combo_token=="synthetic-combo");
    assert(result->passport_mid==verified.passport_mid);
    assert(result->device.device_id==kIdentity.device_id && cloud_checks==1 && queries==2);
    assert((steps==std::vector{PassportStep::Create,PassportStep::Query,PassportStep::Query,PassportStep::Verify,PassportStep::Combo}));
    assert(!qr.empty() && qr.back().empty());
    for(const auto& event:events) assert(event.find("synthetic")==std::string::npos && event.find(kTicket)==std::string::npos);
    auto original=transport.request;
    transport.request=[&](PassportStep step,const std::string& ticket){
        auto value=original(step,ticket);if(step==PassportStep::Verify) value.channel_token.clear();return value;
    };
    cloud_checks=0;result=RunPassportLogin(profile,kIdentity,cancelled,render,progress,transport);
    assert(!result && cloud_checks==0 && qr.back().empty());
    transport.request=[&](PassportStep step,const std::string& ticket){
        auto value=original(step,ticket);if(step==PassportStep::Combo) value.account_id="456";return value;
    };
    result=RunPassportLogin(profile,kIdentity,cancelled,render,progress,transport);
    assert(!result && cloud_checks==0 && events.back()=="PASSPORT_ACCOUNT_MISMATCH");
    transport.request=[&](PassportStep step,const std::string& ticket){
        auto value=original(step,ticket);if(step==PassportStep::Query) value.status=QrStatus::VerificationRequired;return value;
    };
    result=RunPassportLogin(profile,kIdentity,cancelled,render,progress,transport);
    assert(!result && cloud_checks==0 && qr.back().empty() && events.back()=="PASSPORT_VERIFICATION_REQUIRED");
    cancelled=true;steps.clear();
    assert(!RunPassportLogin(profile,kIdentity,cancelled,render,progress,transport) && steps.empty());
    cancelled=false;
    const auto& star=*FindGameProfile("hkrpg");
    for(const bool has_mid:{true,false}) {
        auto verify_data=VerifyData();if(!has_mid) verify_data["user_info"].erase("mid");
        const auto star_verified=ParsePassportReply(PassportStep::Verify,star,200,
            Json({{"retcode",0},{"data",verify_data}}).dump());
        const auto star_combo=ParsePassportReply(PassportStep::Combo,star,200,
            Json({{"retcode",0},{"data",{{"open_id","123"},{"combo_token","synthetic-star-web"},
                {"account_type",1},{"data",R"({"uid":"123"})"}}}}).dump());
        PassportLoginTransport star_transport{
            [&](PassportStep step,const std::string&) {
                if(step==PassportStep::Create) return created;
                if(step==PassportStep::Verify) return star_verified;
                if(step==PassportStep::Combo) return star_combo;
                PassportReply confirmed;confirmed.status=QrStatus::Confirmed;return confirmed;
            },[](std::chrono::milliseconds){return true;}};
        cloud_checks=0;events.clear();
        result=RunPassportLogin(star,kIdentity,cancelled,render,progress,star_transport);
        assert(result.has_value()==has_mid && cloud_checks==(has_mid?1u:0u));
        if(result) assert(result->combo.app_id=="8" && result->passport_mid=="synthetic-mid");
        else assert(events.back()=="PASSPORT_SESSION_CONTEXT_MISSING");
    }
}
