#pragma once

#include <algorithm>
#include <cstdint>

namespace opennow::video {
// One owning network thread. The 7.1.0 transport accepts a PLI no more often
// than every 250 ms and coalesces requests while one is in flight. Retry at
// that fixed cadence for as long as the picture is unresolved: backing off to
// 500/1000 ms leaves the decoder gated after a single lost IDR and is slower
// than the official receiver. A new damage generation never resets the
// cadence; only recovery or an idle gap longer than 250 ms does.
class KeyframeRequestPolicy {
public:
    bool due(std::int64_t now_ms,bool needed,std::uint64_t generation) {
        if(sent_ && now_ms<last_ms_) {sent_=false;active_=false;}
        if(!needed) {active_=false;return false;}
        if(!active_) {
            active_=true;generation_=generation;
            next_ms_=sent_ ? std::max(now_ms,last_ms_+kMinIntervalMs) : now_ms;
        } else if(generation_!=generation) {
            // More damage during the same unresolved recovery must not turn a
            // lossy link into an IDR request storm.
            generation_=generation;
        }
        if(now_ms<next_ms_) return false;
        sent_=true;last_ms_=now_ms;next_ms_=now_ms+kMinIntervalMs;
        return true;
    }
private:
    static constexpr std::int64_t kMinIntervalMs=250;
    bool sent_=false,active_=false;
    std::uint64_t generation_=0;
    std::int64_t last_ms_=0,next_ms_=0;
};
} // namespace opennow::video
