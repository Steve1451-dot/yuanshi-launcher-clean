#include "mihoyo/rtc_route.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin;
    dispatch::SdkStartGameParams params;
    params.game_server_wss_url="wss://primary.invalid";params.game_svr_addr="192.0.2.1:123";params.rtc_udp_port=456;
    params.additional_game_server_wss_urls={"wss://backup.invalid"};params.additional_game_svr_addrs={"198.51.100.2:789"};
    auto routes=rtc::DispatchRoutes(params);
    assert(routes.size()==2 && routes[0].port==456 && routes[1].ip=="198.51.100.2");
    assert(routes[1].signaling_url=="wss://backup.invalid/rtc-sdk");
    const std::string input="candidate:1 1 udp 123 10.0.0.1 1234 typ host generation 0";
    assert(rtc::RouteCandidate(input,routes[1])=="candidate:1 1 udp 123 198.51.100.2 456 typ host generation 0");
    assert(!rtc::RouteCandidate("candidate:1 1 tcp 123 10.0.0.1 9 typ host",routes[0]));
    params.additional_game_svr_addrs.clear();assert(rtc::DispatchRoutes(params).size()==1);
    params.rtc_udp_port=0;assert(rtc::DispatchRoutes(params).empty());
    params.game_control_channel_url="https://primary.invalid/gcc";
    params.additional_game_control_channel_urls={"wss://backup.invalid/gcc"};
    params.game_server_wss_url_ipv6="wss://[2001:db8::1]";
    params.game_control_channel_url_ipv6="https://[2001:db8::1]/gcc";
    auto control=rtc::WebControlRoutes(params);
    assert(control.size()==3 && control[0].control_url==params.game_control_channel_url);
    assert(control[1].signaling_url=="wss://backup.invalid/rtc-sdk" && control[1].control_url=="wss://backup.invalid/gcc");
    assert(control[2].control_url==params.game_control_channel_url_ipv6);
    params.additional_game_control_channel_urls.clear();
    assert(rtc::WebControlRoutes(params)[1].control_url==params.game_control_channel_url);
    params.additional_game_control_channel_urls={""};
    assert(rtc::WebControlRoutes(params)[1].control_url.empty()); // explicit empty is not another server's URL
    for(auto bad:{"256.1.1.1","01.2.3.4","1.2.3.4:5","1.2.3","1.2.3.4.evil","1.2.3.-1"}) assert(!rtc::ValidIpv4(bad));
}
