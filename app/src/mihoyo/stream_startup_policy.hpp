#pragma once
#include "mihoyo/game_control.hpp"
#include "mihoyo/stream_quality_policy.hpp"
#include <cstdint>

namespace cloudgenshin {
// User-requested default: lowest cloud-quality tier and 60 FPS.
// These are requests, not proof that the server has applied them.
inline constexpr int kStartupStreamFps=60;
inline constexpr int kStartupGraphicsMode=control::kGraphicsExtremeSaving;
inline constexpr std::uint32_t kStartupBitRate=MihoyoBitrate(kStreamQualityBitrates[kStartupGraphicsMode].maximum);

// Network-worker clock/counters only. Decoded pictures are not game-loading
// progress. A still scene that continues decoding must never cause a reset.
class VideoProgressWatchdog {
public:
    enum class State {Inactive,Healthy,WaitingPackets,WaitingDecode};
    struct Decision {State state;bool changed=false,resync=false;};
    Decision update(std::int64_t now,unsigned received,unsigned decoded,bool connected) {
        if(!started_ || !connected || received<received_ || decoded<decoded_) {
            started_=true;received_=received;decoded_=decoded;received_at_decode_=received;
            last_receive_=last_decode_=now;resynced_=false;
            return transition(connected ? State::Healthy : State::Inactive);
        }
        if(received!=received_) {received_=received;last_receive_=now;}
        if(decoded!=decoded_) {
            decoded_=decoded;received_at_decode_=received;last_decode_=now;resynced_=false;
        }
        State next=State::Healthy;
        // Initial first-frame timeout is owned by the stream view. Here we
        // recover freezes after at least one successful decoded picture.
        if(decoded) {
            if(now-last_receive_>=3000) next=State::WaitingPackets;
            else if(now-last_decode_>=1500 && received-received_at_decode_>=3) next=State::WaitingDecode;
        }
        auto result=transition(next);
        if(next==State::WaitingDecode && !resynced_ && now-last_decode_>=5000) {
            result.resync=true;resynced_=true;
        }
        return result;
    }
    static bool stalled(State state) {return state==State::WaitingPackets || state==State::WaitingDecode;}
private:
    Decision transition(State next) {const bool changed=state_!=next;state_=next;return {next,changed,false};}
    bool started_=false,resynced_=false;
    State state_=State::Inactive;
    unsigned received_=0,decoded_=0,received_at_decode_=0;
    std::int64_t last_receive_=0,last_decode_=0;
};
} // namespace cloudgenshin
