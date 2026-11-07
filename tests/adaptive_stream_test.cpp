#include "mihoyo/adaptive_stream_policy.hpp"
#include <cassert>

using cloudgenshin::AdaptiveStream;
using cloudgenshin::kStreamQualityBitrates;
struct Link {
    AdaptiveStream policy;
    AdaptiveStream::Sample sample;
    std::int64_t now=0;
    explicit Link(int tier=2,int fps=60) {assert(policy.configure(kStreamQualityBitrates[tier],fps,now));policy.observe(now,sample);}
    void window(unsigned good=120,unsigned damage=0,unsigned congestion=0) {
        now+=2000;sample.completed+=good;sample.damaged+=damage;sample.congestion+=congestion;
        policy.observe(now,sample);
    }
    AdaptiveStream::Adjustment deliver() {
        const auto adjustment=policy.write_due(now);assert(adjustment);
        assert(!policy.sent(now,true));
        assert(!policy.write_due(now+499));
        for(int i=1;i<=2;++i) {
            now+=500;assert(policy.write_due(now));assert(policy.sent(now,true)==(i==2));
        }
        assert(!policy.write_due(now));return *adjustment;
    }
};
int main() {
    Link link;
    assert(!link.policy.configure({2999,4000},60,0));
    assert(!link.policy.configure({3000,3000},120,0));
    // A repaired burst and a single damaged window do not reduce quality.
    link.window();link.window(116,4);link.window();
    assert(!link.policy.write_due(link.now));
    link.window(116,4);link.window(116,4);
    assert(link.deliver().kind==AdaptiveStream::Kind::Bitrate);
    assert(link.policy.bitrate().maximum==3000 && link.policy.fps()==60);
    // Sustained damage at the absolute floor falls back to 30 FPS, not <3 Mbps.
    for(int i=0;i<5;++i) link.window(100,20);
    assert(link.deliver().kind==AdaptiveStream::Kind::Fps);
    assert(link.policy.fps()==30 && link.policy.protecting());
    for(int i=0;i<21;++i) link.window(60);
    assert(!link.policy.write_due(link.now));
    for(int i=0;i<2;++i) link.window(60);
    const auto restore=link.deliver();
    assert(restore.kind==AdaptiveStream::Kind::Fps && restore.fps==60);
    assert(link.policy.bitrate().maximum==3000);
    for(int i=0;i<15;++i) link.window();
    assert(link.deliver().bitrate.maximum==3300);
    // High quality is a ceiling; severe damage can temporarily reduce its
    // floor down to the global 3 Mbps guardrail. It never changes graphics mode.
    Link high(0);
    for(int i=0;i<40;++i) {
        high.window(80,40);
        if(high.policy.write_due(high.now)) {
            // The first attempt was reserved by write_due above.
            high.policy.sent(high.now,true);
            for(int retry=0;retry<2;++retry) {
                high.now+=500;assert(high.policy.write_due(high.now));high.policy.sent(high.now,true);
            }
        }
        assert(high.policy.bitrate().minimum>=3000 && high.policy.bitrate().maximum<=12000);
    }
    assert(high.policy.bitrate().maximum==3000 && high.policy.fps()==30);
    // Manual 30 FPS is never automatically raised. Decode queue congestion
    // alone lowers FPS without sacrificing bitrate or counting display skips.
    Link manual(1,30);
    for(int i=0;i<60;++i) manual.window(60);
    assert(!manual.policy.write_due(manual.now) && manual.policy.fps()==30);
    Link decoder;
    for(int i=0;i<3;++i) decoder.window(120,0,1);
    assert(decoder.deliver().kind==AdaptiveStream::Kind::Fps);
    assert(decoder.policy.bitrate().maximum==4000);
    // No traffic, counter reset, backward clock and missing samples cannot
    // invent recovery or loss. Manual/reconnect suspension cancels stale work.
    const auto before=decoder.policy.fps();
    for(int i=0;i<30;++i) decoder.window(0);
    assert(!decoder.policy.write_due(decoder.now) && decoder.policy.fps()==before);
    decoder.sample={};decoder.window();assert(!decoder.policy.write_due(decoder.now));
    decoder.policy.observe(0,decoder.sample);decoder.now+=20000;
    decoder.policy.observe(decoder.now,decoder.sample);assert(!decoder.policy.write_due(decoder.now));
    Link cancel;
    for(int i=0;i<3;++i) cancel.window(100,20);
    cancel.policy.suspend();assert(!cancel.policy.write_due(cancel.now));
    cancel.policy.manual_fps(30,cancel.now);
    assert(cancel.policy.fps()==30 && cancel.policy.bitrate().maximum==4000);
    for(const int cause:{0,1,2}) {
        Link reset;
        for(int i=0;i<3;++i) reset.window(100,20);
        // A control write is due, but the SSRC/clock/monitoring gap invalidates
        // its old pressure evidence before it is sent.
        const auto at=cause==1 ? 0 : cause==2 ? reset.now+7000 : reset.now+1000;
        reset.policy.observe(at,cause==0 ? AdaptiveStream::Sample{} : reset.sample);
        assert(!reset.policy.write_due(at) && reset.policy.bitrate().maximum==4000);
    }
    // Failed sends are bounded and do not claim an applied bitrate.
    Link failed;
    for(int i=0;i<3;++i) failed.window(100,20);
    for(int i=0;i<20;++i) {
        assert(failed.policy.write_due(failed.now));assert(!failed.policy.sent(failed.now,false));failed.now+=500;
    }
    assert(!failed.policy.write_due(failed.now) && failed.policy.bitrate().maximum==4000);
    // A partial delivery records the accepted request, but retries still stop
    // after 20 attempts. A manual choice gets a fresh delivery budget.
    Link partial;
    for(int i=0;i<3;++i) partial.window(100,20);
    for(int i=0;i<20;++i) {
        assert(partial.policy.write_due(partial.now));
        assert(partial.policy.sent(partial.now,i==0)==(i==19));partial.now+=500;
    }
    assert(!partial.policy.write_due(partial.now) && partial.policy.bitrate().maximum==3000);
    assert(partial.policy.configure(kStreamQualityBitrates[0],60,partial.now));
    for(int i=0;i<3;++i) partial.window(80,40);
    assert(partial.deliver().bitrate.maximum==6000);
    // Clean recovery respects every selected tier, including manual 30 FPS.
    for(int tier=0;tier<3;++tier) {
        Link recovery(tier,30);
        for(int i=0;i<40;++i) {
            recovery.window(40,20);
            if(recovery.policy.write_due(recovery.now)) {
                recovery.policy.sent(recovery.now,true);
                for(int retry=0;retry<2;++retry) {
                    recovery.now+=500;assert(recovery.policy.write_due(recovery.now));
                    recovery.policy.sent(recovery.now,true);
                }
            }
        }
        assert(recovery.policy.bitrate().maximum==3000 && recovery.policy.fps()==30);
        for(int i=0;i<400;++i) {
            recovery.window(60);
            if(const auto change=recovery.policy.write_due(recovery.now)) {
                assert(change->kind==AdaptiveStream::Kind::Bitrate);
                recovery.policy.sent(recovery.now,true);
                for(int retry=0;retry<2;++retry) {
                    recovery.now+=500;assert(recovery.policy.write_due(recovery.now));
                    recovery.policy.sent(recovery.now,true);
                }
            }
            assert(recovery.policy.bitrate().maximum<=kStreamQualityBitrates[tier].maximum);
        }
        const auto range=recovery.policy.bitrate();
        assert(range.minimum==kStreamQualityBitrates[tier].minimum && range.maximum==kStreamQualityBitrates[tier].maximum);
        assert(!recovery.policy.protecting() && recovery.policy.fps()==30);
    }
}
