#pragma once
#include "VideoFrameTiming.hpp"
#include <cstdint>
namespace opennow::video {
// UI-owned 90 kHz video cadence, not an audio clock or a new jitter queue.
// Healthy sequential frames incur no fixed startup delay. Long stalls and
// timestamp/stream changes re-anchor instead of accumulating playback lag.
class NativePresentationClock {
public:
    void reset() {started_=false;}
    std::uint64_t rebases() const {return rebases_;}
    std::uint32_t target(std::int64_t now_us,std::uint32_t front,
                         std::uint32_t newest,std::uint64_t newest_generation,
                         std::uint64_t epoch) {
        if(!started_ || epoch!=epoch_) {
            started_=true;epoch_=epoch;anchor_us_=now_us;anchor_rtp_=front;
            last_us_=now_us;last_generation_=newest_generation;return front;
        }
        const bool fresh=newest_generation!=last_generation_;
        std::uint32_t result=anchor_rtp_;
        const bool paused=now_us<last_us_ || now_us-last_us_>250000;
        if(!paused) {
            const auto elapsed=static_cast<std::uint64_t>(now_us-anchor_us_);
            result+=static_cast<std::uint32_t>((elapsed/100)*9+(elapsed%100)*9/100);
        }
        const std::int64_t skew=RtpDelta(newest,result);
        if(paused || (fresh && (skew>9000 || skew< -9000))) {
            anchor_us_=now_us;anchor_rtp_=newest;result=newest;++rebases_;
        }
        last_us_=now_us;last_generation_=newest_generation;
        return result;
    }
private:
    bool started_=false;
    std::int64_t anchor_us_=0,last_us_=0;
    std::uint32_t anchor_rtp_=0;
    std::uint64_t last_generation_=0,epoch_=0,rebases_=0;
};
}
