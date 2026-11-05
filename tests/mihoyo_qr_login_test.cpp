#include "mihoyo/qr_login.hpp"
#include "mihoyo/production_profile.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <cassert>
#include <algorithm>

int main() {
    using namespace cloudgenshin;
    using namespace cloudgenshin::auth;
    using Json = nlohmann::json;
    const auto& hk4e=DefaultGameProfile();
    const auto& sr=*FindGameProfile("hkrpg");
    assert(Json::parse(*BuildQrFetchBody(hk4e,"test-device"))["app_id"] == 4);
    assert(Json::parse(*BuildQrFetchBody(sr,"test-device"))["app_id"] == 8);
    assert(Json::parse(*BuildQrQueryBody(hk4e,"test-device", "test-ticket"))["device"] == "test-device");
    assert(Json::parse(*BuildQrQueryBody(sr,"test-device", "test-ticket"))["app_id"] == 8);
    assert(!BuildQrFetchBody(hk4e,"bad\nvalue"));
    assert(!BuildQrHeaders(hk4e,"bad\r\nHeader: injected"));
    const auto headers=*BuildQrHeaders(hk4e,"test-device");
    assert(std::find(headers.begin(),headers.end(),"x-rpc-device_id: test-device")!=headers.end());
    const auto sr_headers=*BuildQrHeaders(sr,"test-device");
    assert(std::find(sr_headers.begin(),sr_headers.end(),"x-rpc-app_id: 8")!=sr_headers.end());
    assert(std::find(sr_headers.begin(),sr_headers.end(),"x-rpc-game_biz: hkrpg_cn")!=sr_headers.end());
    const auto fetch = [](const GameProfile& profile,const std::string& url) {
        return ParseQrFetchResponse(profile,200, Json{{"retcode",0},{"data",{{"url",url}}}}.dump());
    };
    const std::string url = "https://user.mihoyo.com/login?ticket=test%2Dticket&app_id=4&biz_key=hk4e_cn&bbs=";
    auto result = fetch(hk4e,url);
    assert(result.status == QrStatus::Ready && result.challenge->ticket == "test-ticket");
    assert(fetch(hk4e,"https://mihoyo.com.evil.invalid/?ticket=x&app_id=4&biz_key=hk4e_cn").status == QrStatus::InvalidResponse);
    assert(fetch(hk4e,"https://evil@mihoyo.com/?ticket=x&app_id=4&biz_key=hk4e_cn").status == QrStatus::InvalidResponse);
    assert(fetch(hk4e,url+"&ticket=duplicate").status == QrStatus::InvalidResponse);
    assert(fetch(hk4e,url+"#fragment").status == QrStatus::InvalidResponse);
    // A challenge is game scoped: another game's app_id/biz_key must not pass.
    const std::string sr_url = "https://user.mihoyo.com/login?ticket=test%2Dticket&app_id=8&biz_key=hkrpg_cn&bbs=";
    assert(fetch(sr,sr_url).status == QrStatus::Ready);
    assert(fetch(hk4e,sr_url).status == QrStatus::InvalidResponse);
    assert(fetch(sr,url).status == QrStatus::InvalidResponse);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":0,"data":{"stat":"Init"}})").status == QrStatus::Waiting);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":0,"data":{"stat":"Scanned"}})").status == QrStatus::Scanned);
    Json raw{{"app_id",4},{"channel_id","1"},{"open_id","different-open-id"},{"combo_token","synthetic-token"},{"uid","123"}};
    Json response{{"retcode",0},{"data",{{"stat","Confirmed"},{"payload",{{"proto","Combo"},{"raw",raw.dump()}}}}}};
    result = ParseQrQueryResponse(hk4e,200,response.dump());
    assert(result.status == QrStatus::Confirmed && result.authorization && result.account_id == "123");
    raw["combo_id"] = 9223372036854775000LL;
    raw["account_type"] = 1;
    raw["heartbeat"] = true;
    raw["asterisk_name"] = "测***试";
    raw["data"] = Json{{"guest", false}, {"inner", {{"flag", true}}},
                        {"open_id", "must-not-override"}, {"channel_token", "not-a-passport-session"}}.dump();
    response["data"]["payload"]["raw"] = raw.dump();
    result = ParseQrQueryResponse(hk4e,200,response.dump());
    assert(result.authorization);
    const auto sdk = Json::parse(result.authorization->sdk_login_data);
    assert(sdk["combo_id"] == 9223372036854775000LL && sdk["account_type"] == 1);
    assert(sdk["heartbeat"] == true && sdk["asterisk_name"] == "测***试");
    assert(sdk["guest"] == false && sdk["inner"]["flag"] == true);
    assert(!sdk.contains("open_id") && !sdk.contains("channel_token"));
    assert(result.authorization->open_id == "different-open-id");
    raw["data"] = "invalid-json"; response["data"]["payload"]["raw"] = raw.dump();
    assert(ParseQrQueryResponse(hk4e,200,response.dump()).status == QrStatus::InvalidResponse);
    raw["data"] = Json{{"large", std::string(4096, 'x')}}.dump();
    response["data"]["payload"]["raw"] = raw.dump();
    assert(ParseQrQueryResponse(hk4e,200,response.dump()).status == QrStatus::InvalidResponse);
    for (const auto key : {"data", "combo_id", "account_type", "heartbeat", "asterisk_name"}) raw.erase(key);
    raw.erase("uid"); response["data"]["payload"]["raw"] = raw.dump();
    result = ParseQrQueryResponse(hk4e,200,response.dump());
    assert(result.status == QrStatus::Confirmed && result.account_id.empty());
    raw["open_id"] = "123456"; response["data"]["payload"]["raw"] = raw.dump();
    result = ParseQrQueryResponse(hk4e,200,response.dump());
    assert(result.status == QrStatus::Confirmed && result.account_id == "123456");
    for (const auto& invalid : {"0", "0123", "9223372036854775808", "12a3", "-123"}) {
        raw["open_id"] = invalid; response["data"]["payload"]["raw"] = raw.dump();
        assert(ParseQrQueryResponse(hk4e,200,response.dump()).account_id.empty());
    }
    raw["open_id"] = "123456"; raw["channel_id"] = "2";
    response["data"]["payload"]["raw"] = raw.dump();
    assert(ParseQrQueryResponse(hk4e,200,response.dump()).account_id.empty());
    raw["channel_id"] = "1";
    raw["uid"] = "123"; raw["aid"] = "456"; response["data"]["payload"]["raw"] = raw.dump();
    assert(ParseQrQueryResponse(hk4e,200,response.dump()).status == QrStatus::InvalidResponse);
    response["data"]["payload"]["proto"] = "Other";
    assert(ParseQrQueryResponse(hk4e,200,response.dump()).status == QrStatus::UnsupportedProtocol);
    response["data"]["payload"]={{"proto","Account"},{"raw",Json{{"uid","123"},{"token","synthetic-account-token"}}.dump()}};
    result=ParseQrQueryResponse(hk4e,200,response.dump());
    assert(result.status==QrStatus::Confirmed && result.account_authorization && !result.authorization);
    assert(result.account_id=="123" && result.account_authorization->token=="synthetic-account-token");
    response["data"]["payload"]["raw"]=R"({"uid":"123"})";
    assert(ParseQrQueryResponse(hk4e,200,response.dump()).status==QrStatus::InvalidResponse);
    // The same confirmed payload must be accepted for another game profile and
    // rejected when the raw app_id belongs to a different game.
    Json sr_raw{{"app_id",8},{"channel_id","1"},{"open_id","different-open-id"},{"combo_token","synthetic-token"},{"uid","123"}};
    Json sr_response{{"retcode",0},{"data",{{"stat","Confirmed"},{"payload",{{"proto","Combo"},{"raw",sr_raw.dump()}}}}}};
    result=ParseQrQueryResponse(sr,200,sr_response.dump());
    assert(result.status==QrStatus::Confirmed && result.account_id=="123");
    sr_raw["app_id"]=4; sr_response["data"]["payload"]["raw"]=sr_raw.dump();
    assert(ParseQrQueryResponse(sr,200,sr_response.dump()).status!=QrStatus::Confirmed);
    const auto risk=ParseQrQueryResponse(hk4e,200,R"({"retcode":-3503,"message":"请求失败，当前设备或网络环境存在风险"})");
    assert(risk.status==QrStatus::RiskRejected && risk.retcode==-3503 && !risk.authorization && !risk.account_authorization);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":-3503,"message":"unrecognized"})").status==QrStatus::Rejected);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":-3501})").status==QrStatus::Expired);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":-3505})").status==QrStatus::Cancelled);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":-3101})").status==QrStatus::VerificationRequired);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":-3235})").status==QrStatus::VerificationRequired);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":-102})").status == QrStatus::Rejected);
    assert(ParseQrQueryResponse(hk4e,302,"{}").status == QrStatus::HttpError);
    assert(ParseQrQueryResponse(hk4e,200,R"({"retcode":18446744073709551615})").status == QrStatus::InvalidResponse);
    assert(ParseQrFetchResponse(hk4e,200,std::string(65537,'x')).status == QrStatus::InvalidResponse);
}
