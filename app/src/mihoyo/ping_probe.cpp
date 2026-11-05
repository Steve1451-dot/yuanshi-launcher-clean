#include "mihoyo/ping_probe.hpp"
#include "mihoyo/sdk_start_game_params.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <bit>
#include <limits>
#include <set>

namespace cloudgenshin::dispatch {
namespace {
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes):bytes_(bytes){}
    bool empty() const {return offset_==bytes_.size();}
    bool integer(std::uint64_t& value) {
        value=0;
        for(unsigned i=0;i<10;++i) {
            if(empty()) return false;
            const auto b=bytes_[offset_++];if(i==9 && b>1) return false;
            value|=static_cast<std::uint64_t>(b&127)<<(i*7);
            if(!(b&128)) return true;
        }return false;
    }
    bool bytes(std::span<const std::uint8_t>& value) {
        std::uint64_t length;
        if(!integer(length) || length>bytes_.size()-offset_) return false;
        value=bytes_.subspan(offset_,static_cast<std::size_t>(length));offset_+=length;return true;
    }
    bool text(std::string& value) {
        std::span<const std::uint8_t> data;if(!bytes(data) || data.size()>4096) return false;
        for(const auto c:data) if(c<32 || c==127) return false;
        if(data.empty()) value.clear();else value.assign(reinterpret_cast<const char*>(data.data()),data.size());
        try {(void)nlohmann::json(value).dump();}catch(...){return false;}return true;
    }
    bool skip(unsigned wire) {
        std::uint64_t v;std::span<const std::uint8_t> data;
        if(wire==0) return integer(v);
        if(wire==2) return bytes(data);
        const auto count=wire==1 ? 8U : wire==5 ? 4U : 0U;
        if(!count || count>bytes_.size()-offset_) return false;
        offset_+=count;return true;
    }
private:std::span<const std::uint8_t> bytes_;std::size_t offset_=0;
};
bool Key(Reader& reader,unsigned& field,unsigned& wire) {
    std::uint64_t key;
    if(!reader.integer(key) || key>std::numeric_limits<std::uint32_t>::max() || !(key>>3)) return false;
    field=static_cast<unsigned>(key>>3);wire=static_cast<unsigned>(key&7);return true;
}
std::optional<PingServer> ParseServer(std::span<const std::uint8_t> data) {
    Reader r(data);PingServer server;unsigned fields=0;
    while(!r.empty()) {
        unsigned field,wire;if(++fields>64 || !Key(r,field,wire)) return std::nullopt;
        std::string* text=nullptr;
        switch(field) {
            case 2:text=&server.addr;break;case 3:text=&server.display_name;break;
            case 4:text=&server.region_id;break;case 7:text=&server.node_id;break;
            case 8:text=&server.wss_url;break;default:break;
        }
        if(text) {if(wire!=2 || !r.text(*text)) return std::nullopt;continue;}
        if(field==1 || field==5 || field==6 || field==9) {
            std::uint64_t value;if(wire!=0 || !r.integer(value)) return std::nullopt;
            if(field==5) server.province_id=std::bit_cast<std::int64_t>(value);
            else if(field==9) {if(value>1) return std::nullopt;server.ipv6=value!=0;}
            else {if(value>0xffffffffULL) return std::nullopt;
                if(field==1) server.id=static_cast<std::uint32_t>(value);else server.isp_type=static_cast<std::uint32_t>(value);}
        } else if(!r.skip(wire)) return std::nullopt;
    }
    if(!server.id) return std::nullopt;
    return server;
}
}
std::optional<std::vector<PingServer>> ParsePingServers(std::span<const std::uint8_t> protobuf) {
    if(protobuf.empty() || protobuf.size()>64*1024) return std::nullopt;
    Reader r(protobuf);std::vector<PingServer> servers;std::set<std::uint32_t> ids;unsigned fields=0;
    while(!r.empty()) {
        unsigned field,wire;if(++fields>1024 || !Key(r,field,wire)) return std::nullopt;
        if(field!=1) {if(!r.skip(wire)) return std::nullopt;continue;}
        std::span<const std::uint8_t> data;
        if(wire!=2 || servers.size()==128 || !r.bytes(data)) return std::nullopt;
        auto server=ParseServer(data);if(!server || !ids.insert(server->id).second) return std::nullopt;
        servers.push_back(std::move(*server));
    }
    return servers;
}
std::optional<std::vector<PingServer>> DecodePingServers(const std::string& base64) {
    const auto data=DecodeSdkBase64(base64);return data ? ParsePingServers(*data) : std::nullopt;
}
std::optional<std::string> BuildMeasuredPingNode(const std::vector<PingSample>& samples) {
    if(samples.empty() || samples.size()>32) return std::nullopt;
    auto list=nlohmann::json::array();std::set<std::uint32_t> ids;
    for(const auto& sample:samples) {
        const auto& s=sample.server;
        if(!s.id || !ids.insert(s.id).second || sample.rtt_ms>10000) return std::nullopt;
        list.push_back({{"ping_server_id",s.id},{"node_id",s.node_id},{"display_name",s.display_name},
            {"region_id",s.region_id},{"province_id",std::to_string(s.province_id)},{"isp_type",s.isp_type},
            {"rtt",sample.rtt_ms},{"loss_rate",0},{"jitter",0},{"out_of_order",0}});
    }
    // Official SDK supports skipping bandwidth. Zero means NOT MEASURED,
    // never a made-up throughput value. RTT values come only from WSS echoes.
    return nlohmann::json{{"bandwidth",0},{"ping_results",list}}.dump();
}
} // namespace cloudgenshin::dispatch
