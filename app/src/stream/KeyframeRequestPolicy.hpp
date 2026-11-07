#pragma once

#include <algorithm>
#include <cstdint>

namespace opennow::video {
// One owning network thread. Request immediately, retry once after 250 ms,
// then allow 500/1000 ms for a large IDR to arrive. Continuous loss must not
// provoke four large intra frames per second on an already congested link.
// Brief recoveries and changing generations keep the retry budget; two quiet
// seconds restore the fast path. Retries remain bounded in interval, not count.
class KeyframeRequestPolicy {
public:
    bool due(std::int64_t now_ms,bool needed,std::uint64_t generation) {
        (void)generation; // More damage cannot bypass the shared retry gate.
        if(sent_ && now_ms<last_ms_) {sent_=false;interval_ms_=250;}
        if(!needed) {
            if(!quiet_) {quiet_=true;quiet_since_ms_=now_ms;}
            return false;
        }
        const bool recovered=quiet_ && now_ms-quiet_since_ms_>=2000;
        quiet_=false;
        if(recovered) {interval_ms_=250;next_ms_=sent_ ? last_ms_+250 : now_ms;}
        if(!sent_) next_ms_=now_ms;
        if(now_ms<next_ms_) return false;
        sent_=true;last_ms_=now_ms;next_ms_=now_ms+interval_ms_;
        interval_ms_=std::min<std::int64_t>(1000,interval_ms_*2);
        return true;
    }
    // A locally rejected write cannot provoke a server IDR. Preserve the fast
    // retry when transport is still opening, without bypassing the 250ms gate.
    void failed() {interval_ms_=250;next_ms_=last_ms_+250;}
private:
    bool sent_=false,quiet_=false;
    std::int64_t last_ms_=0,next_ms_=0,quiet_since_ms_=0,interval_ms_=250;
};
} // namespace opennow::video
