#pragma once
#include <algorithm>
#include <cstdint>

namespace cloudgenshin::endfield {
struct ExitDrainResult {
    bool accepted_locally=false;
    unsigned attempts=0;
    std::int64_t elapsed_ms=0;
};

// Only the owning network worker may run this. A successful send queues SCTP
// bytes; it is NOT an acknowledgement that the cloud game has terminated.
// Keep pumping for up to 500 ms after acceptance, with a 750 ms total bound.
// Retry only locally rejected writes (25 ms apart, at most the first 250 ms).
// Pump must itself be bounded; pause must yield rather than busy-spin.
template<class Connected,class Send,class Pump,class Now,class Pause>
ExitDrainResult DrainExitCommand(Connected&& connected,Send&& send,Pump&& pump,
                                Now&& now,Pause&& pause) {
    ExitDrainResult result;
    const auto started=now();
    auto deadline=started+750, next_send=started;
    while(connected()) {
        const auto current=now();
        if(current>=deadline || (!result.accepted_locally && current>=started+250)) break;
        if(!result.accepted_locally && current>=next_send) {
            ++result.attempts;
            if(send()) {
                result.accepted_locally=true;
                deadline=std::min(deadline,now()+500);
            } else next_send=current+25;
        }
        pump();
        if(now()<deadline) pause();
    }
    result.elapsed_ms=std::max<std::int64_t>(0,now()-started);
    return result;
}
} // namespace cloudgenshin::endfield
