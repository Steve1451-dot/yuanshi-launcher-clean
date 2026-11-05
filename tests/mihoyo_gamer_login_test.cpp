#include "mihoyo/gamer_login.hpp"
#include "mihoyo/production_profile.hpp"

#include <algorithm>
#include <cassert>
#include <string>

int main() {
    using namespace cloudgenshin;
    using namespace cloudgenshin::auth;
    // Synthetic signing profile: production app_id/game_biz, non-production keys.
    constexpr GameProfile test_profile{"hk4e-test","hk4e_cn","测试","Test",
        "https://api-cloudgame.mihoyo.com/hk4e_cg_cn","https://hk4e-sdk.mihoyo.com/hk4e_cn",
        "test-key","test-key","clgm_cn","1.0.0","1.0.0",4,"19","",""};
    const ComboLoginFields fields{"A", "C", "O", "T"};
    const WebDeviceIdentity device{
        "Mozilla/5.0", "WebPC", "1.0", "Windows", "synthetic-id",
        "test-device", "test-model", "hk4e_cn", "zh-cn", "",
    };
    const auto request = BuildGamerLoginRequest(test_profile, fields, device);
    assert(request);
    assert(request->url == "https://api-cloudgame.mihoyo.com/hk4e_cg_cn/gamer/api/login");
    assert(request->headers.size() == 14);
    const auto has_header = [&](const std::string& value) {
        return std::find(request->headers.begin(), request->headers.end(), value) !=
               request->headers.end();
    };
    assert(has_header("x-rpc-combo_token: ai=A;ci=C;oi=O;ct=T;si=75390aed8fba0373b42cbca6f6ee0da531b3542e7a2def468fdc59698490e057;bi=hk4e_cn"));
    assert(has_header("x-rpc-app_id: A"));
    assert(!BuildGamerLoginRequest(test_profile, fields,
        {"Mozilla/5.0", "WebPC", "1.0", "Windows", "id\r\nInjected: yes",
         "test-device", "test-model", "hk4e_cn", "zh-cn", ""}));

    // Per-game wiring: game URL, game_biz header and device identity follow
    // the selected profile.
    const auto& sr=*FindGameProfile("hkrpg");
    const auto sr_device=NativeWebIdentity(sr,"synthetic-id");
    assert(sr_device.op_biz=="clgm_hkrpg-cn" && sr_device.app_version=="4.6.0" &&
        sr_device.client_type=="19" && sr_device.user_agent.starts_with("CloudGenshin-NX/"));
    const auto sr_request=BuildGamerLoginRequest(sr,{"8","1","O","T"},sr_device);
    assert(sr_request && sr_request->url=="https://cg-hkrpg-api.mihoyo.com/hkrpg_cn/cg/gamer/api/login");
    assert(std::find(sr_request->headers.begin(),sr_request->headers.end(),
        "x-rpc-cg_game_biz: hkrpg_cn")!=sr_request->headers.end());
    auto override_profile=sr;override_profile.cloud_app_id="synthetic-cloud-app";
    const auto override_request=BuildGamerLoginRequest(override_profile,{"8","1","O","T"},sr_device);
    assert(override_request && std::find(override_request->headers.begin(),override_request->headers.end(),
        "x-rpc-app_id: synthetic-cloud-app")!=override_request->headers.end());

    assert(ParseGamerLoginResponse(200, R"({"retcode":0,"data":{}})").status ==
           GamerLoginStatus::Success);
    const auto rejected = ParseGamerLoginResponse(200, R"({"retcode":-100,"message":"no"})");
    assert(rejected.status == GamerLoginStatus::Rejected && rejected.retcode == -100);
    assert(ParseGamerLoginResponse(429, "{}").status == GamerLoginStatus::HttpError);
    assert(ParseGamerLoginResponse(200, "not json").status == GamerLoginStatus::InvalidResponse);
    assert(ParseGamerLoginResponse(200, R"({"retcode":"0"})").status == GamerLoginStatus::InvalidResponse);
    assert(ParseGamerLoginResponse(200, R"({"retcode":18446744073709551615})").status ==
           GamerLoginStatus::InvalidResponse);
}
