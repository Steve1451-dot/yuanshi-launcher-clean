#include "stream/KeyframeRequestPolicy.hpp"
#include <cassert>

int main() {
    using opennow::video::KeyframeRequestPolicy;
    KeyframeRequestPolicy policy;
    assert(!policy.due(0,false,1));
    assert(policy.due(100,true,1)); // First loss requests immediately.
    assert(!policy.due(349,true,1));
    assert(policy.due(350,true,1)); // One fast retry, then allow the IDR to arrive.
    assert(!policy.due(600,true,1));
    assert(!policy.due(849,true,1));
    assert(policy.due(850,true,1));
    assert(!policy.due(1100,true,1));
    assert(!policy.due(1150,false,1)); // Recovered: no periodic IDR spam.
    assert(!policy.due(1360,true,2)); // A brief green interval cannot reset pacing.
    assert(!policy.due(1400,true,3)); // Repeated damage stays rate limited.
    assert(!policy.due(1849,true,4));
    assert(policy.due(1850,true,4));
    assert(!policy.due(1861,true,7));
    assert(!policy.due(1900,false,7));
    assert(!policy.due(3899,false,7));
    assert(policy.due(3900,true,8)); // Two stable seconds restore the fast path.
    assert(policy.due(4150,true,8));
    assert(policy.due(0,true,8)); // Monotonic clock reset cannot wedge the gate.

    KeyframeRequestPolicy burst;
    unsigned requests=0;
    for(int t=0;t<1000;++t)
        if(burst.due(t,true,static_cast<unsigned>(t))) ++requests;
    assert(requests==3); // 0/250/750; new generations never bypass the gate.
    for(int t=1000;t<10000;++t)
        if(burst.due(t,true,static_cast<unsigned>(t))) ++requests;
    assert(requests==12); // Continuous recovery retries at most once/second.
    KeyframeRequestPolicy flicker;
    requests=0;
    for(int t=0;t<10000;++t)
        if(flicker.due(t,t%2==0,static_cast<unsigned>(t))) ++requests;
    assert(requests<=12); // Brief recovered frames do not cause an IDR storm.
    KeyframeRequestPolicy opening;
    assert(opening.due(0,true,0));opening.failed();
    assert(!opening.due(249,true,0));
    assert(opening.due(250,true,0));opening.failed();
    assert(opening.due(500,true,0)); // Rejected writes do not delay startup recovery.
}
