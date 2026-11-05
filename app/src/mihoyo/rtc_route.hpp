#pragma once
#include "mihoyo/sdk_start_game_params.hpp"
#include "mihoyo/signaling_endpoint.hpp"
#include "mihoyo/ice_candidate_policy.hpp"
#include <optional>
#include <sstream>
namespace cloudgenshin::rtc {
struct DispatchRoute {std::string signaling_url,ip;std::uint32_t port=0;};
struct WebControlRoute {std::string signaling_url,control_url;};
inline std::vector<WebControlRoute> WebControlRoutes(const dispatch::SdkStartGameParams& params) {
    std::vector<WebControlRoute> routes;
    const auto family=[&](const std::string& primary,const std::vector<std::string>& servers,
                          const std::string& control,const std::vector<std::string>& controls) {
        const auto add=[&](const std::string& server,const std::string& url) {
            const auto endpoint=GameSignalingEndpoint(server);
            if(routes.size()<64 && !endpoint.empty()) routes.push_back({endpoint,url});
        };
        add(primary,control);
        // Official WebSessionInfo selects the same server index; when its GCC
        // list is shorter it falls back to index zero, not another game server.
        for(std::size_t i=0;i<servers.size() && routes.size()<64;++i)
            add(servers[i],i<controls.size() ? controls[i] : control);
    };
    family(params.game_server_wss_url,params.additional_game_server_wss_urls,
           params.game_control_channel_url,params.additional_game_control_channel_urls);
    family(params.game_server_wss_url_ipv6,params.additional_game_server_wss_urls_ipv6,
           params.game_control_channel_url_ipv6,params.additional_game_control_channel_urls_ipv6);
    return routes;
}
inline bool ValidIpv4(std::string_view ip) {
    for(int i=0;i<4;++i) {
        const auto dot=ip.find('.');
        const auto part=ip.substr(0,dot);unsigned number=0;
        const auto parsed=std::from_chars(part.data(),part.data()+part.size(),number);
        if(part.empty() || part.size()>3 || (part.size()>1 && part.front()=='0') ||
           parsed.ec!=std::errc{} || parsed.ptr!=part.data()+part.size() || number>255) return false;
        if(i==3) return dot==ip.npos;
        if(dot==ip.npos) return false;
        ip.remove_prefix(dot+1);
    }
    return false;
}
inline std::vector<DispatchRoute> DispatchRoutes(const dispatch::SdkStartGameParams& params) {
    std::vector<DispatchRoute> routes;
    // Mirrors WebSessionInfo: parallel IPv4 WSS/address lists, separate RTC port.
    // Do not borrow an IPv4 route for an unpaired IPv6/fallback endpoint.
    const auto add=[&](const std::string& wss,const std::string& addr) {
        if(routes.size()>=64 || !params.rtc_udp_port || params.rtc_udp_port>65535) return;
        const auto ip=addr.substr(0,addr.find(':'));
        const auto endpoint=GameSignalingEndpoint(wss);
        if(!endpoint.empty() && ValidIpv4(ip)) routes.push_back({endpoint,ip,params.rtc_udp_port});
    };
    add(params.game_server_wss_url,params.game_svr_addr);
    if(params.additional_game_server_wss_urls.size()==params.additional_game_svr_addrs.size())
        for(std::size_t i=0;i<params.additional_game_server_wss_urls.size();++i)
            add(params.additional_game_server_wss_urls[i],params.additional_game_svr_addrs[i]);
    return routes;
}
inline std::optional<std::string> RouteCandidate(const std::string& candidate,const DispatchRoute& route) {
    if(!ValidIpv4(route.ip) || !route.port || route.port>65535 || ClassifyCandidate(candidate)!=CandidateKind::Udp)
        return std::nullopt;
    std::istringstream in(candidate);std::array<std::string,8> fields;
    for(auto& field:fields) if(!(in>>field)) return std::nullopt;
    fields[4]=route.ip;fields[5]=std::to_string(route.port);
    std::string result;
    for(const auto& field:fields) {if(!result.empty()) result+=' ';result+=field;}
    std::string extension;while(in>>extension) result+=' '+extension;
    return result;
}
}
