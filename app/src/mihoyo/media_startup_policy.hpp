#pragma once
#include <cstdint>
#include <optional>

namespace cloudgenshin {
// Network-worker only. SdkDeviceInfo travels over authenticated signaling, not
// decoded video or input. Never wait for a first frame to describe the screen.
// Failed sends are bounded; a later real geometry replaces the initial display
// size, and channel recovery can explicitly invalidate the last successful send.
class DeviceInfoSync {
public:
    void invalidate() {width_=height_=0;}
    template<class Send>
    bool poll(std::int64_t now,bool authenticated,unsigned width,unsigned height,Send send) {
        if(!authenticated || !width || !height || width>16384 || height>16384 ||
           (width==width_ && height==height_)) return false;
        if(attempt_ && now>=*attempt_ && now-*attempt_<500) return false;
        attempt_=now;
        if(!send(width,height)) return false;
        width_=width;height_=height;return true;
    }
private:
    unsigned width_=0,height_=0;
    std::optional<std::int64_t> attempt_;
};

// One safe, numeric-evidence classification before the 120-second wait limit.
// A ready control channel is NOT proof media was sent or reached the decoder.
// Do not auto-requeue, change identity, synthesize IMU activation or end a pod.
class MediaStartupProbe {
public:
    struct Ingress {
        std::uint64_t rtp=0,decrypt_failures=0,payload_rejected=0,ssrc_rejected=0;
    };
    const char* update(std::int64_t now,bool ready,std::uint64_t video_rtp,
                       std::uint64_t video_units,std::uint64_t audio_packets,std::uint64_t decoded,
                       Ingress ingress) {
        if(reported_ || decoded) {if(decoded) reported_=true;return nullptr;}
        if(!ready) {ready_at_.reset();return nullptr;}
        if(!ready_at_ || now<*ready_at_) ready_at_=now;
        if(now-*ready_at_<20000) return nullptr;
        reported_=true;
        if(video_units) return "MEDIA_STARTUP_VIDEO_UNITS_NOT_DECODED_20S";
        if(video_rtp) return "MEDIA_STARTUP_VIDEO_RTP_NO_UNIT_20S";
        if(audio_packets) return "MEDIA_STARTUP_AUDIO_ONLY_20S";
        if(ingress.rtp) {
            if(ingress.decrypt_failures==ingress.rtp) return "MEDIA_STARTUP_SRTP_REJECTED_20S";
            if(ingress.payload_rejected==ingress.rtp) return "MEDIA_STARTUP_PAYLOAD_REJECTED_20S";
            if(ingress.ssrc_rejected==ingress.rtp) return "MEDIA_STARTUP_SSRC_REJECTED_20S";
            return "MEDIA_STARTUP_RTP_NO_OUTPUT_20S";
        }
        return "MEDIA_STARTUP_NO_AV_20S";
    }
private:
    std::optional<std::int64_t> ready_at_;
    bool reported_=false;
};
}
