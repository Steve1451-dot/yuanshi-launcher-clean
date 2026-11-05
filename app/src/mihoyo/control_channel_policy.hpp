#pragma once
#include <cstdint>

namespace cloudgenshin::rtc {
// Worker-owned, monotonic milliseconds. DTLS/video and usable input are
// separate milestones; neither a missing association nor DCEP may wait forever.
class ControlChannelPolicy {
public:
    enum class State { Inactive, WaitingAssociation, WaitingChannel, Ready, Recovering,
                       InitFailed, AssociationTimeout, ChannelTimeout, Lost };
    void begin(std::int64_t now) { state_=State::WaitingAssociation;deadline_=now+15000; }
    void reset() {state_=State::Inactive;deadline_=0;}
    State update(std::int64_t now,bool init_failed,bool association,bool channel) {
        if(state_==State::Inactive || failed()) return state_;
        if(init_failed) return state_=State::InitFailed;
        if(state_==State::Ready) {
            if(!association || !channel) {state_=State::Recovering;deadline_=now+5000;}
            return state_;
        }
        // Keep receiving media through a brief control outage. Input remains
        // gated until both pieces return; repeated samples never extend the
        // recovery deadline, and a failed initial setup keeps its own policy.
        if(state_==State::Recovering) {
            if(now>=deadline_) return state_=State::Lost;
            if(association && channel) state_=State::Ready;
            return state_;
        }
        if(association && channel) return state_=State::Ready;
        if(association && state_==State::WaitingAssociation) {
            state_=State::WaitingChannel;deadline_=now+15000;
        }
        if(state_==State::WaitingChannel && !association) return state_=State::Lost;
        if(now>=deadline_) state_=state_==State::WaitingAssociation ? State::AssociationTimeout : State::ChannelTimeout;
        return state_;
    }
    State state() const {return state_;}
    bool ready() const {return state_==State::Ready;}
    bool failed() const {return state_==State::InitFailed || state_==State::AssociationTimeout ||
        state_==State::ChannelTimeout || state_==State::Lost;}
private:
    State state_=State::Inactive;
    std::int64_t deadline_=0;
};
}
