#include "mihoyo/signaling_endpoint.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin;
    for (const auto base:{"wss://example.invalid","wss://example.invalid/",
                         "wss://example.invalid/rtc-sdk","wss://example.invalid/rtc-sdk/"})
        assert(GameSignalingEndpoint(base)=="wss://example.invalid/rtc-sdk");
    assert(GameSignalingEndpoint("wss://example.invalid/base?key=synthetic-secret")==
           "wss://example.invalid/base/rtc-sdk?key=synthetic-secret");
    assert(GameSignalingEndpoint("wss://[2001:db8::1]:443?x=1")==
           "wss://[2001:db8::1]:443/rtc-sdk?x=1");
    assert(GameSignalingEndpoint("wss://example.invalid/rtc-sdk?x=1")==
           "wss://example.invalid/rtc-sdk?x=1");
    for (const auto bad:{"ws://example.invalid/","wss://","wss://example.invalid/#x"})
        assert(GameSignalingEndpoint(bad).empty());
    const auto endpoints=GameSignalingEndpoints("wss://example.invalid",{
        "wss://example.invalid/rtc-sdk","wss://backup.invalid/","ws://bad.invalid"});
    assert(endpoints.size()==2 && endpoints[0]=="wss://example.invalid/rtc-sdk" &&
           endpoints[1]=="wss://backup.invalid/rtc-sdk");
    // Shared ping transport must still use exactly its supplied path.
    assert(opennow::websocket::ParseEndpoint("wss://example.invalid/ping")->request_target=="/ping");
}
