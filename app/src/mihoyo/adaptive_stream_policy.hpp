#pragma once
#include "mihoyo/setting_delivery_policy.hpp"
#include "mihoyo/stream_quality_policy.hpp"
#include <algorithm>
#include <cstdint>
#include <optional>

namespace cloudgenshin {
// Network-worker owned. Decisions use completed/destroyed RTP access units and
// actual decoder queue overflows, never display skips, low-motion bitrate or
// absolute ping. The user's selection remains the recovery ceiling.
class AdaptiveStream {
public:
    struct Sample {std::uint32_t completed=0,damaged=0,timeouts=0,capacity=0,congestion=0;};
    enum class Kind {Bitrate,Fps};
    struct Adjustment {Kind kind;StreamBitrateRange bitrate;int fps;};
    bool configure(StreamBitrateRange range,int fps,std::int64_t now) {
        if(range.minimum<kFloor || range.maximum<range.minimum || range.maximum>12000 ||
           (fps!=30 && fps!=60)) return false;
        preferred_=current_=range;preferred_fps_=fps_=fps;
        suspend();next_change_=now+5000;return true;
    }
    void manual_fps(int fps,std::int64_t now) {
        if(fps!=30 && fps!=60) return;
        preferred_fps_=fps_=fps;suspend();next_change_=now+5000;
    }
    // Pause during manual delivery/control-channel recovery. A stale automatic
    // request must not overwrite a newer selection when transport returns.
    void suspend() {pending_.reset();have_sample_=false;bad_=decode_bad_=0;clean_ms_=0;}
    void observe(std::int64_t now,Sample sample) {
        if(!current_.maximum) return;
        const bool restart=have_sample_ && (now<sample_ms_ || now-sample_ms_>6000 || reset(sample));
        if(!have_sample_ || restart) {
            if(restart) {suspend();next_change_=now+5000;}
            previous_=sample;sample_ms_=now;have_sample_=true;
            bad_=decode_bad_=0;clean_ms_=0;return;
        }
        const auto elapsed=now-sample_ms_;
        if(elapsed<2000) return;
        const Sample d{sample.completed-previous_.completed,sample.damaged-previous_.damaged,
            sample.timeouts-previous_.timeouts,sample.capacity-previous_.capacity,
            sample.congestion-previous_.congestion};
        previous_=sample;sample_ms_=now;
        const auto total=static_cast<std::uint64_t>(d.completed)+d.damaged;
        const bool pressure=d.capacity>0 || (d.damaged>=3 &&
            (static_cast<std::uint64_t>(d.damaged)*100>=total*2 || d.timeouts>=3));
        const bool severe=d.capacity>=2 || (d.damaged>=6 &&
            static_cast<std::uint64_t>(d.damaged)*100>=total*10);
        bad_=pressure ? bad_+1 : 0;
        decode_bad_=d.congestion ? decode_bad_+1 : 0;
        const bool clean=d.completed*1000ULL>=static_cast<std::uint64_t>(elapsed)*15 &&
            !d.damaged && !d.timeouts && !d.capacity && !d.congestion;
        clean_ms_=clean ? clean_ms_+elapsed : 0;
        if(pending_ || now<next_change_) return;
        if(decode_bad_>=2 && fps_==60) enqueue(Kind::Fps,current_,30);
        else if(bad_>=2 || severe) {
            if(current_.maximum>kFloor) {
                const int cap=std::max(kFloor,current_.maximum*(severe?2:3)/4);
                enqueue(Kind::Bitrate,{std::min(preferred_.minimum,cap),cap},fps_);
            } else if(fps_==60) enqueue(Kind::Fps,current_,30);
        } else if(fps_<preferred_fps_) {
            // Restore smooth motion first at the proven low bitrate, then
            // require another clean interval before increasing image detail.
            if(clean_ms_>=45000) enqueue(Kind::Fps,current_,preferred_fps_);
        } else if(clean_ms_>=30000 && current_.maximum<preferred_.maximum) {
            const int cap=std::min(preferred_.maximum,current_.maximum+std::max(250,current_.maximum/10));
            enqueue(Kind::Bitrate,{std::min(preferred_.minimum,cap),cap},fps_);
        }
    }
    std::optional<Adjustment> write_due(std::int64_t now) {
        return pending_ && budget_.try_write({revision_},now) ? pending_ : std::nullopt;
    }
    // Accepted means locally queued, not a remote ACK. Repeat each idempotent
    // change with the same bounded budget as manual settings (3/20 writes).
    bool sent(std::int64_t now,bool accepted) {
        if(!pending_) return false;
        budget_.sent(accepted);
        if(accepted) {current_=pending_->bitrate;fps_=pending_->fps;any_accepted_=true;}
        if(!budget_.delivered() && !budget_.exhausted()) return false;
        pending_.reset();bad_=decode_bad_=0;clean_ms_=0;
        next_change_=now+(any_accepted_ ? 8000 : 5000);
        return any_accepted_;
    }
    StreamBitrateRange bitrate() const {return current_;}
    int fps() const {return fps_;}
    bool protecting() const {return current_.maximum<preferred_.maximum || fps_<preferred_fps_;}
private:
    bool reset(Sample s) const {
        return s.completed<previous_.completed || s.damaged<previous_.damaged ||
            s.timeouts<previous_.timeouts || s.capacity<previous_.capacity || s.congestion<previous_.congestion;
    }
    void enqueue(Kind kind,StreamBitrateRange range,int fps) {
        pending_=Adjustment{kind,range,fps};revision_+=1024;any_accepted_=false;
    }
    static constexpr int kFloor=kStreamQualityBitrates[2].minimum;
    StreamBitrateRange preferred_{0,0},current_{0,0};
    int preferred_fps_=60,fps_=60;
    Sample previous_;
    bool have_sample_=false,any_accepted_=false;
    unsigned bad_=0,decode_bad_=0;
    std::int64_t sample_ms_=0,next_change_=0,clean_ms_=0;
    std::uint64_t revision_=0;
    std::optional<Adjustment> pending_;
    overlay::SettingSendBudget budget_;
};
} // namespace cloudgenshin
