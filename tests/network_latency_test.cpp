#include "mihoyo/network_latency.hpp"
#include <cassert>
#include <iostream>
#include <thread>

int main() {
    using cloudgenshin::overlay::NetworkLatency;
    NetworkLatency latency;
    assert(!latency.value(0));
    int calls=0,rtt=15;
    const auto read=[&]{++calls;return rtt;};
    latency.sample(0,read);assert(latency.value(0)==15 && calls==1);
    for(int now=1;now<1000;++now) {latency.sample(now,read);assert(latency.value(now)==15);}
    assert(calls==1); // Even a 1000 Hz caller does not query SCTP per frame.
    rtt=0;latency.sample(1000,read);assert(latency.value(1000)==0 && calls==2);
    assert(!latency.value(999)); // Never show a sample from the future.
    assert(latency.value(6000)==0 && !latency.value(6001));
    rtt=-1;latency.sample(2000,read);assert(!latency.value(2000));
    rtt=60001;latency.sample(3000,read);assert(!latency.value(3000));
    rtt=60000;latency.sample(4000,read);assert(latency.value(4000)==60000);
    latency.clear();assert(!latency.value(4000));
    rtt=22;latency.sample(100,read);assert(latency.value(100)==22); // Clock reset.
    latency.sample(-1,read);assert(calls==6);
    // UI reads never call the transport. Concurrent publications expose one
    // coherent sample, not a new RTT paired with a stale timestamp.
    std::atomic_bool done=false;
    std::thread worker([&]{for(int i=1;i<5000;++i) latency.sample(i*1000,[]{return 17;});done=true;});
    while(!done) {const auto value=latency.value(5000000);assert(!value || *value==17);}
    worker.join();assert(latency.value(4999000)==17);
    latency.clear();assert(!latency.value(4999000));
    std::cout<<"PASS RTT cadence, unknown/zero/range, expiry, reset and atomic UI handoff\n";
}
