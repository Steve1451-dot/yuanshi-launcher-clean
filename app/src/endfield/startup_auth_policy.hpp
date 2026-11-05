#pragma once
#include <cstdint>
#include <string_view>

namespace cloudgenshin::endfield {
// Local defensive deadline, not a claimed server protocol timeout. A local
// SCTP send only means queued bytes; OnAuthResult must confirm pod auth.
class StartupAuthPolicy {
public:
    enum class State { Idle, Waiting, Accepted, Rejected, TimedOut };
    void begin(std::int64_t now) {
        if(state_==State::Idle) {state_=State::Waiting;deadline_=now+30000;}
    }
    void result(std::string_view value) {
        if(value=="success") state_=State::Accepted;
        else if(value=="failure") state_=State::Rejected;
    }
    State poll(std::int64_t now) {
        if(state_==State::Waiting && now>=deadline_) state_=State::TimedOut;
        return state_;
    }
private:
    State state_=State::Idle;
    std::int64_t deadline_=0;
};
} // namespace cloudgenshin::endfield
