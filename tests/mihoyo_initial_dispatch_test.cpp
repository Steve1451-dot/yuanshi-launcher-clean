#include "mihoyo/initial_dispatch.hpp"
#include "mihoyo/production_profile.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <string>

int main() {
    using namespace cloudgenshin;
    // Synthetic signing profile: production app_id/game_biz keep the historical
    // signing vector meaningful while the keys stay non-production.
    constexpr GameProfile test_profile{"hk4e-test","hk4e_cn","测试","Test",
        "https://api-cloudgame.mihoyo.com/hk4e_cg_cn","https://hk4e-sdk.mihoyo.com/hk4e_cn",
        "test-key","test-key","clgm_cn","1.0.0","1.0.0",4,"19","",""};
    const auth::ComboLoginFields fields{"A", "C", "O", "T"};
    const auth::WebDeviceIdentity device{
        "Mozilla/5.0", "WebPC", "1.0", "Windows", "synthetic-id",
        "test-device", "test-model", "hk4e_cn", "zh-cn", "",
    };
    dispatch::InitialDispatchParams params;
    params.bit_rate = 5120000;
    params.fps = 60;
    params.node_id = "node-test";
    params.resolution = "1280x720";
    params.environment = "production";
    params.user_data_json =
        R"({"dpi":192,"w":1280,"h":720,"lang":"zh-cn","di":"synthetic"})";
    params.regions_json = "[1,2]";
    params.net_state = 4;

    const auto request = dispatch::BuildInitialDispatchRequest(
        test_profile, fields, device, params);
    assert(request);
    assert(request->url ==
        "https://api-cloudgame.mihoyo.com/hk4e_cg_cn/dispatcher/api/paasDispatch");
    assert(request->headers.size() == 15);
    assert(std::find(request->headers.begin(), request->headers.end(),
                     "Content-Type: application/json") != request->headers.end());
    const auto body = nlohmann::json::parse(request->body);
    assert(body["bit_rate"] == 5120000);
    assert(body["biz_key"] == "hk4e_cn");
    assert(body["net_state"] == 4);
    assert(body["codec_type"] == 1);
    assert(body["user_data"] == params.user_data_json);
    assert(body["regions"] == nlohmann::json::array({1, 2}));
    assert(body["hint"].is_null());
    assert(body["queue_type"] == "");
    assert(body["using_new_cmd_line"] == true);
    assert(body["sign"] ==
        "878a2dc33fe15fe3b7495d4ef5cd15912119a08bb3418f4d89778d9b4d1ae58a");

    // Per-game dispatch: URL and biz_key follow the selected profile.
    const auto& sr=*FindGameProfile("hkrpg");
    const auto sr_request=dispatch::BuildInitialDispatchRequest(sr,fields,device,params);
    assert(sr_request && sr_request->url==
        "https://cg-hkrpg-api.mihoyo.com/hkrpg_cn/cg/dispatcher/api/paasDispatch");
    assert(nlohmann::json::parse(sr_request->body)["biz_key"]=="hkrpg_cn");
    assert(nlohmann::json::parse(sr_request->body)["sign"]!=body["sign"]);

    params.queue_type = dispatch::QueueType::Coin;
    params.queue_switch = true;
    params.hint = R"({"synthetic":true})";
    const auto coin = dispatch::BuildInitialDispatchRequest(
        test_profile, fields, device, params);
    assert(coin);
    const auto coin_body = nlohmann::json::parse(coin->body);
    assert(coin_body["queue_type"] == "coin");
    assert(coin_body["queue_switch"] == true);
    assert(coin_body["hint"]["synthetic"] == true);
    assert(coin_body["sign"] == body["sign"]);
    // A profile may carry a combo key that differs from the dispatch key: the
    // combo token header changes while the signed body stays identical.
    GameProfile separate_keys=test_profile;separate_keys.combo_app_key="different-combo-key";
    const auto separate=dispatch::BuildInitialDispatchRequest(separate_keys,fields,device,params);
    assert(separate && separate->headers != coin->headers && separate->body == coin->body);
    params.hint="synthetic-hint";
    assert(!dispatch::BuildInitialDispatchRequest(test_profile,fields,device,params));
    params.hint.reset();

    params.regions_json = "[]";
    const auto no_regions = dispatch::BuildInitialDispatchRequest(
        test_profile, fields, device, params);
    assert(no_regions);
    assert(nlohmann::json::parse(no_regions->body)["regions"].empty());
    assert(nlohmann::json::parse(no_regions->body)["sign"] != body["sign"]);

    params.regions_json = "[{}]";
    assert(!dispatch::BuildInitialDispatchRequest(test_profile, fields, device, params));
    params.regions_json = "[1,2]";
    params.fps = 0;
    assert(!dispatch::BuildInitialDispatchRequest(test_profile, fields, device, params));
    params.fps = 60;
    params.node_id = "node\r\nInjected: yes";
    assert(!dispatch::BuildInitialDispatchRequest(test_profile, fields, device, params));
    params.node_id = "node-test";
    params.user_data_json = "not json";
    assert(!dispatch::BuildInitialDispatchRequest(test_profile, fields, device, params));
}
