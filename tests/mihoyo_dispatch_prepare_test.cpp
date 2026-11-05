#include "mihoyo/dispatch_prepare.hpp"
#include "mihoyo/production_profile.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <cassert>

int main() {
    using namespace cloudgenshin;
    using namespace dispatch;
    using Json=nlohmann::json;
    // Synthetic signing profile: production app_id/game_biz, non-production keys.
    constexpr GameProfile test_profile{"hk4e-test","hk4e_cn","测试","Test",
        "https://api-cloudgame.mihoyo.com/hk4e_cg_cn","https://hk4e-sdk.mihoyo.com/hk4e_cn",
        "test-key","test-key","clgm_cn","1.0.0","1.0.0",4,"19","",""};
    const auth::ComboLoginFields fields{"4","1","test-open","test-token"};
    const auto device=NativeWebIdentity(test_profile,"test-device");
    const std::string measured=R"({"bandwidth":0,"ping_results":[{"ping_server_id":17,"rtt":24}]})";
    const auto request=BuildPrepareRequest(test_profile,PrepareOperation::Nodes,fields,device,{},measured);
    assert(request && Json::parse(request->body)["node"] == measured && Json::parse(request->body)["speed_client_type"] == 7);
    assert(!BuildPrepareRequest(test_profile,PrepareOperation::Nodes,fields,device));
    assert(!BuildPrepareRequest(test_profile,PrepareOperation::Nodes,fields,device,{},R"({"bandwidth":0,"ping_results":[]})"));
    assert(!BuildPrepareRequest(test_profile,PrepareOperation::Nodes,fields,device,{},R"({"bandwidth":0,"ping_results":[{"ping_server_id":17,"rtt":-1}]})"));
    const auto ping=BuildPrepareRequest(test_profile,PrepareOperation::PingServers,fields,device);
    assert(ping && Json::parse(Json::parse(ping->body)["ext_data"].get<std::string>())["platform"]==19);
    const auto config=ParsePrepareResponse(PrepareOperation::PingServers,200,R"({"retcode":0,"data":{"ping_svr":[],"sdk_param":"CgIIEQ=="}})");
    assert(config.status==PrepareStatus::Success && config.ping_sdk_param=="CgIIEQ==");
    const auto verify=BuildPrepareRequest(test_profile,PrepareOperation::Verify,fields,device,{"node-test","[\"region-test\"]",true});
    assert(verify && Json::parse(verify->body)["regions"].is_array() && Json::parse(verify->body)["sign"].is_string());
    assert(!BuildPrepareRequest(test_profile,PrepareOperation::Verify,fields,device,{"bad\nnode","[]",true}));
    // Per-game dispatcher wiring follows the selected profile.
    const auto& sr=*FindGameProfile("hkrpg");
    const auto sr_request=BuildPrepareRequest(sr,PrepareOperation::Nodes,
        auth::ComboLoginFields{"8","1","test-open","test-token"},NativeWebIdentity(sr,"test-device"),{},measured);
    assert(sr_request && sr_request->url=="https://cg-hkrpg-api.mihoyo.com/hkrpg_cn/cg/dispatcher/api/getNodesInfo");
    assert(Json::parse(sr_request->body)["biz_key"]=="hkrpg_cn");
    assert(std::find(sr_request->headers.begin(),sr_request->headers.end(),
        "x-rpc-cg_game_biz: hkrpg_cn")!=sr_request->headers.end());
    assert(ParsePrepareResponse(PrepareOperation::Status,200,R"({"retcode":0,"data":{"maintenance_info":null}})").status==PrepareStatus::Success);
    assert(ParsePrepareResponse(PrepareOperation::Status,200,R"({"retcode":0,"data":{"maintenance_info":{}}})").status==PrepareStatus::Maintenance);
    assert(ParsePrepareResponse(PrepareOperation::Status,200,R"({"retcode":0,"data":{}})").status==PrepareStatus::InvalidResponse);
    auto response=Json{{"retcode",0},{"data",{{"result_code","NONE"},{"nodes",Json::array({{{"node_id","test-node"},{"region_ids",Json::array({"region-1"})},{"recommend",true}}})}}}};
    auto result=ParsePrepareResponse(PrepareOperation::Nodes,200,response.dump());
    assert(result.status==PrepareStatus::Success && result.nodes.size()==1 && result.nodes[0].recommended);
    response["data"]["nodes"][0]["node_id"]="bad\r\nvalue";
    assert(ParsePrepareResponse(PrepareOperation::Nodes,200,response.dump()).status==PrepareStatus::InvalidResponse);
    assert(ParsePrepareResponse(PrepareOperation::Nodes,200,R"({"retcode":0,"data":{"result_code":"NONE","nodes":[]}})").status==PrepareStatus::Unavailable);
    assert(ParsePrepareResponse(PrepareOperation::Nodes,200,R"({"retcode":-100})").status==PrepareStatus::Rejected);
    assert(ParsePrepareResponse(PrepareOperation::Nodes,302,"{}").status==PrepareStatus::HttpError);
}
