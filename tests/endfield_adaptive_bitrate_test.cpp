#include "endfield/adaptive_bitrate.hpp"
#include <cassert>
using cloudgenshin::endfield::AdaptiveBitrate;
int main() {
    AdaptiveBitrate policy;AdaptiveBitrate::Sample sample;
    assert(!policy.observe(0,sample));
    assert(!policy.configure(0,4000,0) && !policy.configure(4000,3000,0));
    assert(policy.configure(1000,4000,0));
    assert(!policy.observe(0,sample));
    sample={280,20,25,0};assert(!policy.observe(5000,sample));
    sample={560,40,50,0};assert(policy.observe(10000,sample)==3000);
    assert(policy.current()==4000); // Enqueued is not remotely applied.
    policy.sent(10000,false);assert(policy.current()==4000);
    sample={840,60,75,0};assert(!policy.observe(15000,sample));
    sample={1120,80,100,0};assert(policy.observe(20000,sample)==3000);
    policy.sent(20000,true);assert(policy.current()==3000);
    // Clean delivery recovers slowly, never beyond the user's selected cap.
    for(int t=25000;t<50000;t+=5000) {sample.completed+=300;assert(!policy.observe(t,sample));}
    sample.completed+=300;assert(policy.observe(50000,sample)==3300);
    policy.sent(50000,true);
    assert(policy.configure(1000,4000,50000) && policy.current()==4000); // Manual choice wins.
    assert(!policy.observe(50000,sample));
    // No new RTP is not evidence of bandwidth pressure or a clean interval.
    for(int t=55000;t<100000;t+=5000) assert(!policy.observe(t,sample));
    sample={};assert(!policy.observe(100000,sample)); // SSRC reset.
    sample={300,0,0,0};assert(!policy.observe(105000,sample));
    assert(!policy.observe(90000,sample)); // Backward clock resets baseline.
    assert(!policy.observe(200000,sample)); // Long monitoring gap is not clean streaming.
    assert(policy.configure(1000,1000,0));assert(!policy.observe(0,{}));
    for(int t=5000;t<200000;t+=5000) {
        sample.completed+=200;sample.damaged+=20;sample.timeouts+=20;
        assert(!policy.observe(t,sample)); // Floor cannot be crossed.
    }
    // A single repaired burst never triggers a reduction.
    assert(policy.configure(1000,4000,0));sample={};assert(!policy.observe(0,sample));
    sample={250,4,5,0};assert(!policy.observe(5000,sample));
    sample.completed+=300;assert(!policy.observe(10000,sample));
    assert(!policy.observe(10001,sample)); // Faster polling does not invent windows.
}
