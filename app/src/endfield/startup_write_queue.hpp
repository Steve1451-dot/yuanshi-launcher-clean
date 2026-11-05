#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace cloudgenshin::endfield {

// Worker-owned, bounded startup sequence. Accepted steps are never replayed;
// failed local writes retry with the same bytes for at most 15 seconds.
class StartupWriteQueue {
public:
    enum class State { Idle, Pending, Ready, Failed };
    void begin(std::vector<std::vector<std::uint8_t>> packets,std::int64_t now) {
        packets_=std::move(packets);next_=0;deadline_=now+15000;retry_=now;
        state_=packets_.empty() || packets_.size()>4 ? State::Failed : State::Pending;
    }
    template<class Send> State poll(std::int64_t now,Send&& send) {
        if(state_!=State::Pending) return state_;
        if(now>=deadline_) return state_=State::Failed;
        if(now<retry_) return state_;
        while(next_<packets_.size()) {
            if(!send(packets_[next_])) {retry_=now+100;return state_;}
            ++next_;
        }
        packets_.clear();return state_=State::Ready;
    }
private:
    std::vector<std::vector<std::uint8_t>> packets_;
    std::size_t next_=0;
    std::int64_t deadline_=0,retry_=0;
    State state_=State::Idle;
};

} // namespace cloudgenshin::endfield
