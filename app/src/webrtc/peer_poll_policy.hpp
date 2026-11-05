#pragma once
#include <cstdint>

namespace opennow::webrtc {
struct PeerDrainResult {unsigned packets=0;bool exhausted=false;};
// Cooperative work budget: cannot preempt a slow callback/socket call.
// Exhaustion is a hint, not proof of queued packets or packet loss.
template<class Poll,class Microseconds>
PeerDrainResult DrainPeerPacketsBudgeted(Poll&& poll,Microseconds&& microseconds) {
    const std::int64_t started=microseconds();
    unsigned packets=0;
    while(packets<64) {
        if(poll()<=0) return {packets,false};
        ++packets;
        if(microseconds()-started>=2000) return {packets,true};
    }
    return {packets,true};
}
template<class Poll,class Microseconds>
unsigned DrainPeerPackets(Poll&& poll,Microseconds&& microseconds) {
    return DrainPeerPacketsBudgeted(poll,microseconds).packets;
}
template<class Drain,class Signal>
void PollMediaAroundSignaling(Drain&& drain,Signal&& signal) {
    drain();signal();drain();
}
} // namespace opennow::webrtc
