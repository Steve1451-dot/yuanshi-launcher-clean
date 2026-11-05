#include "stream/StageTimings.hpp"
#include <cassert>
#include <thread>
int main() {
    opennow::video::StageTimings timings;
    assert(timings.mean()==0 && timings.maximum()==0);
    timings.observe(1000);timings.observe(3000);timings.observe(-1);
    assert(timings.mean()==1333 && timings.maximum()==3000);
    assert(timings.takeIntervalMaximum()==3000 && timings.takeIntervalMaximum()==0);
    timings.observe(1000);assert(timings.takeIntervalMaximum()==1000 && timings.maximum()==3000);
    timings.reset();
    std::thread a([&]{for(int i=0;i<10000;++i) timings.observe(1000);});
    std::thread b([&]{for(int i=0;i<10000;++i) timings.observe(3000);});
    a.join();b.join();
    assert(timings.mean()==2000 && timings.maximum()==3000);
    const auto totals=timings.snapshot();assert(totals.count==20000 && totals.total==40000000);
    timings.observe(16667);timings.observe(16668);timings.observe(33334);timings.observe(33335);
    const auto tails=timings.snapshot();assert(tails.over_16ms==3 && tails.over_33ms==1);
}
