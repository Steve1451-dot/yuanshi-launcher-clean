#include "mihoyo/media_startup_policy.hpp"
#include <cassert>
#include <string_view>
#include <iostream>
int main() {
    cloudgenshin::DeviceInfoSync sync;
    unsigned attempts=0;bool accepted=false;
    const auto send=[&](unsigned w,unsigned h) {++attempts;assert(w==1280 && h==720);return accepted;};
    assert(!sync.poll(0,false,1280,720,send) && attempts==0);
    assert(!sync.poll(0,true,0,720,send) && attempts==0);
    assert(!sync.poll(0,true,1280,720,send) && attempts==1);
    assert(!sync.poll(499,true,1280,720,send) && attempts==1);
    accepted=true;assert(sync.poll(500,true,1280,720,send) && attempts==2);
    assert(!sync.poll(1000,true,1280,720,send));
    sync.invalidate();assert(sync.poll(1000,true,1280,720,send) && attempts==3);
    assert(!sync.poll(1200,true,1920,1080,[](auto,auto){assert(false);return false;}));
    assert(sync.poll(1500,true,1920,1080,[](auto w,auto h){return w==1920 && h==1080;}));
    sync.invalidate();assert(sync.poll(1,true,1280,720,send)); // Clock rollback must not prevent retries forever.
    using cloudgenshin::MediaStartupProbe;
    for(unsigned stage=0;stage<4;++stage) {
        MediaStartupProbe probe;
        const auto update=[&](long long now,bool ready) {return probe.update(now,ready,stage>=2,stage==3,stage==1,0,{});};
        assert(!update(0,false));assert(!update(100,true));assert(!update(20099,true));
        const auto tag=update(20100,true);assert(tag);
        constexpr const char* tags[]={"MEDIA_STARTUP_NO_AV_20S","MEDIA_STARTUP_AUDIO_ONLY_20S",
            "MEDIA_STARTUP_VIDEO_RTP_NO_UNIT_20S","MEDIA_STARTUP_VIDEO_UNITS_NOT_DECODED_20S"};
        assert(std::string_view(tag)==tags[stage]);assert(!update(30000,true));
    }
    MediaStartupProbe good;assert(!good.update(0,true,0,0,0,0,{}));
    assert(!good.update(21000,true,1,1,0,1,{}) && !good.update(50000,true,1,1,0,0,{}));
    MediaStartupProbe interrupted;assert(!interrupted.update(0,true,0,0,0,0,{}));
    assert(!interrupted.update(19999,false,0,0,0,0,{}));assert(!interrupted.update(20000,true,0,0,0,0,{}));
    assert(!interrupted.update(39999,true,0,0,0,0,{}));assert(interrupted.update(40000,true,0,0,0,0,{}));
    MediaStartupProbe rollback;assert(!rollback.update(50000,true,0,0,0,0,{}));
    assert(!rollback.update(1,true,0,0,0,0,{}));assert(rollback.update(20001,true,0,0,0,0,{}));
    const MediaStartupProbe::Ingress evidence[]={{5,5,0,0},{5,0,5,0},{5,0,0,5},{5,2,1,1}};
    const char* tags[]={"MEDIA_STARTUP_SRTP_REJECTED_20S","MEDIA_STARTUP_PAYLOAD_REJECTED_20S",
        "MEDIA_STARTUP_SSRC_REJECTED_20S","MEDIA_STARTUP_RTP_NO_OUTPUT_20S"};
    for(unsigned i=0;i<4;++i) {
        MediaStartupProbe probe;
        assert(!probe.update(0,true,0,0,0,0,evidence[i]));
        assert(std::string_view(probe.update(20000,true,0,0,0,0,evidence[i]))==tags[i]);
        assert(!probe.update(30000,true,0,0,0,0,evidence[i]));
    }
    // Receiving only audio or already-decoded frames must not be reclassified
    // as a complete transport failure by an earlier rejected packet.
    MediaStartupProbe audio;assert(!audio.update(0,true,0,0,1,0,{5,5,0,0}));
    assert(std::string_view(audio.update(20000,true,0,0,1,0,{5,5,0,0}))=="MEDIA_STARTUP_AUDIO_ONLY_20S");
    std::cout<<"PASS authenticated pre-frame device info, bounded retry/geometry/recovery + one-shot media startup diagnosis\n";
}
