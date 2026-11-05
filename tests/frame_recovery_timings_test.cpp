#include "stream/FrameRecoveryTimings.hpp"
#include <cassert>

int main() {
    opennow::video::FrameRecoveryTimings timings;
    assert(timings.snapshot().phase==0 && timings.admit(true,1)==0);
    timings.begin(1000);timings.begin(5000); // A second loss preserves the first start.
    assert(timings.snapshot().episodes==1 && timings.admit(false,6000)==0);
    const auto first=timings.admit(true,11000);
    assert(first && timings.snapshot().phase==2 && timings.snapshot().idr_last_us==10000);
    timings.output(0,true,12000);timings.output(first,false,13000);
    assert(timings.snapshot().completed==0);
    assert(timings.admit(false,14000)==first);
    timings.output(first,true,16000);
    auto s=timings.snapshot();
    assert(s.completed==1 && s.phase==0 && s.output_last_us==5000 && s.total_last_us==15000);
    timings.output(first,true,18000);assert(timings.snapshot().completed==1);

    timings.begin(20000);const auto second=timings.admit(true,40000);
    timings.begin(41000); // New damage after an IDR was queued, before it decoded.
    timings.output(second,true,42000); // Must not close the newer recovery.
    assert(timings.snapshot().phase==1 && timings.snapshot().completed==1);
    const auto third=timings.admit(true,45000);assert(third!=second);
    timings.output(second,true,46000);assert(timings.snapshot().phase==2);
    timings.output(third,true,47000);s=timings.snapshot();
    assert(s.episodes==3 && s.completed==2 && s.superseded==1);
    assert(s.idr_last_us==4000 && s.idr_max_us==20000);
    assert(s.output_last_us==2000 && s.output_max_us==5000);
    assert(s.total_last_us==6000 && s.total_max_us==15000);

    // A monotonic-clock discontinuity must not wrap into an enormous latency.
    timings.begin(60000);const auto clock_reset=timings.admit(true,50000);
    timings.output(clock_reset,true,40000);s=timings.snapshot();
    assert(s.idr_last_us==0 && s.output_last_us==0 && s.total_last_us==0);
    timings.reset();s=timings.snapshot();
    assert(s.phase==0 && s.episodes==0 && s.total_max_us==0);
    assert(timings.admit(true,70000)==0);
}
