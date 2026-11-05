#pragma once
#include <cstdint>
#include <algorithm>

namespace cloudgenshin::overlay {
// Moonlight-style stage separation, fed by this protocol's real counters.
// UI-thread totals; no peer locks, file writes or packet logging.
struct StreamHealthTotals {
    std::uint64_t received=0,decoded=0,presented=0,bytes=0;
    std::uint64_t decode_count=0,decode_us=0,queue_count=0,queue_us=0;
    std::uint64_t waiting_idr=0,congestion=0,display_skips=0;
    std::uint64_t audio_played=0,audio_drops=0,audio_underruns=0;
    std::uint64_t render_count=0,render_us=0,poll_peak_us=0;
    std::uint64_t ui_frames=0,ui_peak_us=0,poll_recent_us=0,ui_slow=0;
    std::uint64_t command_busy=0,upload_busy=0,hw_ref_fail=0,mapping_fail=0,hw_frames=0;
    std::uint64_t decode_over16=0,decode_over33=0,decode_peak_us=0;
    std::uint64_t ui_draws=0,ui_input_us=0,ui_video_us=0,ui_tail_us=0,ui_outside_peak_us=0;
};
struct StreamHealthWindow {
    bool ready=false,ui_active=false;
    double received_fps=0,decoded_fps=0,submitted_fps=0,mbps=0;
    double decode_ms=0,queue_ms=0,audio_played_ms=0;
    std::uint64_t waiting_idr=0,congestion=0,display_skips=0;
    std::uint64_t audio_drops=0,audio_underruns=0;
    double render_ms=0,poll_peak_ms=0;
    double ui_fps=0,ui_peak_ms=0,poll_recent_ms=0;
    std::uint64_t ui_slow=0,command_busy=0,upload_busy=0,hw_ref_fail=0,mapping_fail=0,hw_frames=0;
    std::uint64_t sample=0,duration_ms=0;
    std::uint64_t decode_over16=0,decode_over33=0;
    double decode_peak_ms=0,ui_input_ms=0,ui_video_ms=0,ui_tail_ms=0,ui_outside_peak_ms=0;
};
class StreamHealth {
public:
    bool sample(std::int64_t now,const StreamHealthTotals& totals,bool ui_active) {
        pending_ui_=std::max(pending_ui_,totals.ui_peak_us);
        pending_poll_=std::max(pending_poll_,totals.poll_recent_us);
        pending_decode_=std::max(pending_decode_,totals.decode_peak_us);
        pending_outside_=std::max(pending_outside_,totals.ui_outside_peak_us);
        ui_seen_=ui_seen_ || ui_active || ui_active!=last_ui_;last_ui_=ui_active;
        if(!started_ || now<last_ms_) {started_=true;last_=totals;last_ms_=now;return false;}
        const auto elapsed=now-last_ms_;if(elapsed<1000) return false;
        const auto d=[](auto a,auto b) {return a>=b ? a-b : 0;};
        const auto dc=d(totals.decode_count,last_.decode_count),qc=d(totals.queue_count,last_.queue_count);
        const auto rc=d(totals.render_count,last_.render_count);
        window_={true,ui_seen_,
            d(totals.received,last_.received)*1000.0/elapsed,
            d(totals.decoded,last_.decoded)*1000.0/elapsed,
            d(totals.presented,last_.presented)*1000.0/elapsed,
            d(totals.bytes,last_.bytes)*0.008/elapsed,
            dc ? d(totals.decode_us,last_.decode_us)/(dc*1000.0) : 0,
            qc ? d(totals.queue_us,last_.queue_us)/(qc*1000.0) : 0,
            d(totals.audio_played,last_.audio_played)/48.0,
            d(totals.waiting_idr,last_.waiting_idr),d(totals.congestion,last_.congestion),
            d(totals.display_skips,last_.display_skips),
            d(totals.audio_drops,last_.audio_drops),d(totals.audio_underruns,last_.audio_underruns),
            rc ? d(totals.render_us,last_.render_us)/(rc*1000.0) : 0,totals.poll_peak_us/1000.0};
        window_.ui_fps=d(totals.ui_frames,last_.ui_frames)*1000.0/elapsed;
        window_.ui_peak_ms=pending_ui_/1000.0;window_.poll_recent_ms=pending_poll_/1000.0;
        window_.ui_slow=d(totals.ui_slow,last_.ui_slow);
        window_.command_busy=d(totals.command_busy,last_.command_busy);
        window_.upload_busy=d(totals.upload_busy,last_.upload_busy);
        window_.hw_ref_fail=d(totals.hw_ref_fail,last_.hw_ref_fail);
        window_.mapping_fail=d(totals.mapping_fail,last_.mapping_fail);
        window_.hw_frames=d(totals.hw_frames,last_.hw_frames);
        window_.sample=++samples_;window_.duration_ms=elapsed;
        window_.decode_over16=d(totals.decode_over16,last_.decode_over16);
        window_.decode_over33=d(totals.decode_over33,last_.decode_over33);
        window_.decode_peak_ms=pending_decode_/1000.0;
        const auto draws=d(totals.ui_draws,last_.ui_draws);
        window_.ui_input_ms=draws ? d(totals.ui_input_us,last_.ui_input_us)/(draws*1000.0) : 0;
        window_.ui_video_ms=draws ? d(totals.ui_video_us,last_.ui_video_us)/(draws*1000.0) : 0;
        window_.ui_tail_ms=draws ? d(totals.ui_tail_us,last_.ui_tail_us)/(draws*1000.0) : 0;
        window_.ui_outside_peak_ms=pending_outside_/1000.0;
        pending_ui_=pending_poll_=pending_decode_=pending_outside_=0;
        last_=totals;last_ms_=now;ui_seen_=ui_active;return true;
    }
    const StreamHealthWindow& window() const {return window_;}
private:
    bool started_=false,ui_seen_=false,last_ui_=false;
    std::int64_t last_ms_=0;
    std::uint64_t pending_ui_=0,pending_poll_=0,samples_=0;
    std::uint64_t pending_decode_=0,pending_outside_=0;
    StreamHealthTotals last_;
    StreamHealthWindow window_;
};
}
