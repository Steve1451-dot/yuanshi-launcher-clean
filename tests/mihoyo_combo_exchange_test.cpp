#include "mihoyo/combo_exchange.hpp"
#include "mihoyo/combo_login_data.hpp"
#include "mihoyo/production_profile.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <cassert>

int main() {
    using namespace cloudgenshin;
    using namespace cloudgenshin::auth;
    using Json=nlohmann::json;
    // Synthetic signing profile: the production app_id/game_biz keep the
    // historical signing vector meaningful while the keys stay non-production.
    constexpr GameProfile test_profile{"hk4e-test","hk4e_cn","测试","Test",
        "https://api-cloudgame.mihoyo.com/hk4e_cg_cn","https://hk4e-sdk.mihoyo.com/hk4e_cn",
        "test-key","test-key","clgm_cn","1.0.0","1.0.0",4,"19","",""};
    const auto& sr=*FindGameProfile("hkrpg");
    const auto request=BuildComboExchangeRequest(test_profile,{"123","synthetic-account-token"},"test-device");
    assert(request);
    const auto body=Json::parse(request->body);
    assert(body["app_id"]==4 && body["channel_id"]==1 && body["device"]=="test-device");
    assert(body["sign"]=="9f4220e86de3075a807d4ded1d15246b926ac444c1cafefe2067da79d1a51717");
    const auto data=Json::parse(body["data"].get<std::string>());
    assert(data["uid"]=="123" && data["token"]=="synthetic-account-token" && data["guest"]==false);
    assert(!BuildComboExchangeRequest(test_profile,{"123","token"},"bad\r\nHeader:1"));
    assert(!BuildComboExchangeRequest(test_profile,{"9223372036854775808","token"},"device"));
    assert(!BuildComboExchangeRequest(test_profile,{"123","bad\nvalue"},"device"));
    const auto sr_request=BuildComboExchangeRequest(sr,{"123","synthetic-account-token"},"test-device");
    assert(sr_request);
    assert(Json::parse(sr_request->body)["app_id"]==8);
    assert(Json::parse(sr_request->body)["sign"]!=body["sign"]);
    auto result=ParseComboExchangeResponse(test_profile,200,R"({"retcode":0,"data":{"open_id":"123","combo_token":"synthetic-combo","account_type":1}})");
    assert(result.status==QrStatus::Confirmed && result.authorization && !result.account_authorization);
    assert(result.authorization->combo_token=="synthetic-combo" && result.authorization->app_id=="4");
    assert(Json::parse(result.authorization->sdk_login_data)["account_type"] == 1);
    const auto full=ParseComboExchangeResponse(test_profile,200,
        R"({"retcode":0,"data":{"open_id":"123","combo_token":"synthetic-combo","combo_id":"1234567890123456789","account_type":1,"heartbeat":false,"data":"{\"guest\":false,\"role_flag\":7}"}})");
    assert(full.authorization);
    const auto sdk=Json::parse(full.authorization->sdk_login_data);
    assert(sdk["combo_id"]=="1234567890123456789" && sdk["heartbeat"]==false && sdk["role_flag"]==7);
    const auto sr_result=ParseComboExchangeResponse(sr,200,R"({"retcode":0,"data":{"open_id":"123","combo_token":"synthetic-combo","account_type":1}})");
    assert(sr_result.status==QrStatus::Confirmed && sr_result.authorization && sr_result.authorization->app_id=="8");
    const auto h5_body=Json{{"retcode",0},{"data",{{"open_id","123"},{"combo_token","synthetic-combo"},
        {"account_type",1},{"combo_id","42"},{"heartbeat",true},{"fatigue_remind",Json::object()},
        {"root_flag",true},{"root_object",{{"enabled",false}}},{"data",Json{{"root_flag",false},{"guest",false}}.dump()}}}};
    const auto h5=ParseComboExchangeResponse(sr,200,h5_body.dump());
    assert(h5.authorization && h5.sdk_h5_additional_root_fields==2);
    const auto h5_data=Json::parse(h5.authorization->sdk_login_data);
    assert(h5_data["root_flag"]==false && h5_data["root_object"]["enabled"]==false && h5_data["guest"]==false);
    assert(!h5_data.contains("heartbeat") && !h5_data.contains("fatigue_remind") && !h5_data.contains("combo_token"));
    const auto unchanged=ParseComboExchangeResponse(test_profile,200,h5_body.dump());
    assert(unchanged.authorization && unchanged.sdk_h5_additional_root_fields==0);
    const auto unchanged_data=Json::parse(unchanged.authorization->sdk_login_data);
    assert(!unchanged_data.contains("root_object") && unchanged_data["heartbeat"]==true);
    const std::string too_deep=std::string(1800,'[')+"0"+std::string(1800,']');
    assert(!DecodeComboSdkData(too_deep));
    assert(ParseComboExchangeResponse(test_profile,200,R"({"retcode":0,"data":{"open_id":"123"}})").status==QrStatus::InvalidResponse);
    assert(ParseComboExchangeResponse(test_profile,200,R"({"retcode":-100,"data":{"open_id":"123","combo_token":"untrusted"}})").status==QrStatus::Rejected);
    assert(ParseComboExchangeResponse(test_profile,200,R"({"retcode":18446744073709551615})").status==QrStatus::InvalidResponse);
    assert(ParseComboExchangeResponse(test_profile,302,"{}").status==QrStatus::HttpError);
    assert(ParseComboExchangeResponse(test_profile,200,std::string(65537,'x')).status==QrStatus::InvalidResponse);
}
