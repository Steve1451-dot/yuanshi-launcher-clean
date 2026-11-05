#include "stream/KeyframeRequestPolicy.hpp"
#include <cassert>

int main() {
    using opennow::video::KeyframeRequestPolicy;
    KeyframeRequestPolicy policy;
    assert(!policy.due(0,false,1));
    assert(policy.due(100,true,1)); // First loss requests immediately.
    assert(!policy.due(349,true,1));
    assert(policy.due(350,true,1)); // Fixed 250 ms cadence.
    assert(!policy.due(599,true,1));
    assert(policy.due(600,true,1));
    assert(!policy.due(849,true,1));
    assert(policy.due(850,true,1));
    assert(!policy.due(1099,true,1));
    assert(policy.due(1100,true,1));
    assert(!policy.due(1150,false,1)); // Recovered: no periodic IDR spam.
    assert(policy.due(1360,true,2)); // New loss waits only for the 250 ms gate.
    assert(!policy.due(1400,true,3)); // Repeated damage stays rate limited.
    assert(!policy.due(1609,true,4));
    assert(policy.due(1610,true,4)); // Constant cadence, no exponential backoff.
    assert(!policy.due(1650,false,5));
    assert(!policy.due(1700,true,6)); // New loss still respects the last request.
    assert(policy.due(1860,true,6));
    assert(!policy.due(1861,true,7));
    assert(policy.due(0,true,8)); // Monotonic clock reset cannot wedge the gate.

    KeyframeRequestPolicy burst;
    unsigned requests=0;
    for(int t=0;t<1000;++t)
        if(burst.due(t,true,static_cast<unsigned>(t))) ++requests;
    assert(requests==4); // Coalesce damage to 4/s: 0/250/500/750.
}
