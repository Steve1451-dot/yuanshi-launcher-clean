#include "mihoyo/ping_probe.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <cassert>
int main() {
    using namespace cloudgenshin::dispatch;
    auto minimal=DecodePingServers("CgIIEQ==");
    assert(minimal && minimal->size()==1 && minimal->front().id==17);
    // Independently assembled protobuf: ListPingSvrRsp.1 -> PingSvrInfo.
    const std::vector<std::uint8_t> data={0x0a,0x0e,0x08,0x11,0x1a,4,'t','e','s','t',0x3a,2,'n','1',0x48,1};
    const auto decoded=ParsePingServers(data);
    assert(decoded && decoded->front().display_name=="test" && decoded->front().node_id=="n1" && decoded->front().ipv6);
    auto bad=data;bad.pop_back();assert(!ParsePingServers(bad));
    bad=data;bad.insert(bad.end(),data.begin(),data.end());assert(!ParsePingServers(bad));
    assert(!DecodePingServers("not base64"));
    assert(!ParsePingServers(std::vector<std::uint8_t>{0x0a,2,8,0}));
    assert(!ParsePingServers(std::vector<std::uint8_t>{0x0a,2,0x0b,0}));
    assert(!ParsePingServers(std::vector<std::uint8_t>(65537,0)));
    const auto node=BuildMeasuredPingNode({{decoded->front(),24}});
    assert(node);
    const auto json=nlohmann::json::parse(*node);
    assert(json["bandwidth"]==0 && json["ping_results"][0]["rtt"]==24 && json["ping_results"][0]["ping_server_id"]==17);
    assert(json["ping_results"][0]["province_id"]=="0");
    assert(!BuildMeasuredPingNode({}));
    const std::vector<PingSample> duplicates{{decoded->front(),24},{decoded->front(),25}};
    assert(!BuildMeasuredPingNode(duplicates));
    const std::vector<PingSample> too_slow{{decoded->front(),10001}};
    assert(!BuildMeasuredPingNode(too_slow));
}
