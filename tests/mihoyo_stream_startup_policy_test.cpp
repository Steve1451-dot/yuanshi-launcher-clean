#include "mihoyo/stream_startup_policy.hpp"
#include <cassert>

int main() {
    using namespace cloudgenshin;
    using S=VideoProgressWatchdog::State;
    static_assert(kStartupStreamFps==60 && kStartupGraphicsMode==control::kGraphicsHighQuality);
    static_assert(kStartupBitRate==12000*1024);
    VideoProgressWatchdog watch;
    assert(watch.update(0,0,0,false).state==S::Inactive);
    assert(watch.update(10,0,0,true).state==S::Healthy);
    // First picture has a separate bounded timeout; no repeated decoder resets.
    assert(!watch.update(5000,100,0,true).resync);
    assert(watch.update(5010,101,1,true).state==S::Healthy);
    assert(watch.update(6000,130,30,true).state==S::Healthy);
    auto result=watch.update(7499,170,30,true);assert(result.state==S::Healthy && !result.resync);
    result=watch.update(7500,171,30,true);assert(result.state==S::WaitingDecode && result.changed && !result.resync);
    result=watch.update(8000,186,30,true);assert(result.state==S::WaitingDecode && !result.changed && !result.resync);
    result=watch.update(11000,276,30,true);assert(result.state==S::WaitingDecode && result.resync);
    assert(!watch.update(12000,306,30,true).resync); // One reset per stalled interval.
    result=watch.update(12010,307,31,true);assert(result.state==S::Healthy && result.changed);
    // No incoming packets is not a decoder failure and must not flush references.
    result=watch.update(15010,307,31,true);assert(result.state==S::WaitingPackets && result.changed && !result.resync);
    assert(!watch.update(20000,307,31,true).resync);
    result=watch.update(20010,308,32,true);assert(result.state==S::Healthy);
    // A cloud-side static/loading scene still sends decodable pictures; never
    // infer game progress or inject a click based on pixel/content changes.
    for(int i=1;i<=200;++i) {
        result=watch.update(20010+i*33,308+i,32+i,true);
        assert(result.state==S::Healthy && !result.resync);
    }
    // Disconnect / new counters reset the sampling baseline.
    assert(watch.update(30000,508,232,false).state==S::Inactive);
    assert(watch.update(31000,1,0,true).state==S::Healthy);
    assert(!watch.update(32000,20,1,true).resync);
    assert(VideoProgressWatchdog::stalled(S::WaitingDecode));
    assert(VideoProgressWatchdog::stalled(S::WaitingPackets));
    assert(!VideoProgressWatchdog::stalled(S::Healthy));
}
