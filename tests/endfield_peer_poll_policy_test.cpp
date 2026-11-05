#include "endfield/peer_poll_policy.hpp"
#include <cassert>
#include <iostream>
#include <string>
using cloudgenshin::endfield::DrainPeerPackets;
int main() {
    std::string order;
    cloudgenshin::endfield::PollMediaAroundSignaling([&]{order+='v';},[&]{order+='s';});
    assert(order=="vsv");
    static_assert(cloudgenshin::endfield::kVideoRecoveryHoldMs==120);
    static_assert(cloudgenshin::endfield::kVideoRecoveryInitialPackets==512);
    static_assert(cloudgenshin::endfield::kVideoRecoveryMaximumPackets==2048);
    int calls=0;
    assert(DrainPeerPackets([&]{++calls;return 0;},[]{return 0;})==0);
    assert(calls==1);
    calls=0;
    auto idle=cloudgenshin::endfield::DrainPeerPacketsBudgeted([&]{++calls;return 0;},[]{return 0;});
    assert(idle.packets==0 && !idle.exhausted && calls==1);
    calls=0;
    auto busy=cloudgenshin::endfield::DrainPeerPacketsBudgeted([&]{++calls;return 1;},[]{return 0;});
    assert(busy.packets==64 && busy.exhausted);
    calls=0;std::int64_t burst_clock=0;
    busy=cloudgenshin::endfield::DrainPeerPacketsBudgeted([&]{++calls;burst_clock+=500;return 1;},[&]{return burst_clock;});
    assert(busy.packets==4 && busy.exhausted);
    calls=0;
    assert(DrainPeerPackets([&]{return ++calls<=12?1:0;},[]{return 0;})==12);
    assert(calls==13);
    calls=0;
    assert(DrainPeerPackets([&]{++calls;return 1;},[]{return 0;})==64);
    assert(calls==64);
    std::int64_t now=0;calls=0;
    assert(DrainPeerPackets([&]{++calls;now+=500;return 1;},[&]{return now;})==4);
    assert(calls==4); // Timers regain control within 2 ms under sustained load.
    calls=0;
    assert(DrainPeerPackets([&]{++calls;return -1;},[]{return 0;})==0);
    assert(calls==1);
    std::cout<<"PASS bounded peer burst drain\n";
}
