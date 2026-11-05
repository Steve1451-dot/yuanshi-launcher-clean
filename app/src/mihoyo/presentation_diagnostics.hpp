#pragma once
#include "mihoyo/stream_health.hpp"
#include "stream/IVideoRenderer.hpp"
#include <array>
#include <atomic>
#include <climits>
#include <utility>
#include <chrono>
namespace cloudgenshin::overlay {
// UI-owned measurement, atomic scalar mirror for the existing 5 s safe logger.
// No disk I/O, allocation, raw frames, credentials or per-frame logging here.
class PresentationDiagnostics {
public:
    static std::int64_t nowMicros() {return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
    void beginDraw(std::int64_t now=nowMicros()) {
        if(draw_ended_ && now>=draw_end_) outside_peak_=std::max(outside_peak_,static_cast<std::uint64_t>(now-draw_end_));
        draw_start_=now;draw_active_=true;video_active_=tail_active_=false;
        input_current_=video_current_=0;
    }
    void videoBegin(std::int64_t now=nowMicros()) {
        if(draw_active_ && now>=draw_start_) input_current_=now-draw_start_;
        video_start_=now;video_active_=draw_active_;
    }
    void endDraw(std::int64_t now=nowMicros()) {
        if(tail_active_ && now>=tail_start_) {input_total_+=input_current_;video_total_+=video_current_;tail_total_+=now-tail_start_;++draws_;}
        draw_end_=now;draw_ended_=true;draw_active_=video_active_=tail_active_=false;
    }
    void observe(std::int64_t now,const VideoRenderStats* stats) {
        if(video_active_ && now>=video_start_) {video_current_=now-video_start_;tail_start_=now;tail_active_=true;}
        if(started_ && now>=last_) {
            const auto gap=static_cast<std::uint64_t>(now-last_);
            peak_=std::max(peak_,gap);session_peak_=std::max(session_peak_,gap);if(gap>50000) ++slow_;
        }
        started_=true;last_=now;++frames_;
        if(stats) stats_=*stats;
    }
    void append(StreamHealthTotals& t,std::uint64_t poll_recent,std::uint64_t hardware_frames) {
        t.ui_frames=frames_;t.ui_peak_us=std::exchange(peak_,0);t.ui_slow=slow_;
        t.poll_recent_us=poll_recent;t.command_busy=stats_.command_busy;t.upload_busy=stats_.upload_busy;
        t.hw_ref_fail=stats_.hardware_ref_failed;t.mapping_fail=stats_.mapping_failed;t.hw_frames=hardware_frames;
        t.ui_draws=draws_;t.ui_input_us=input_total_;t.ui_video_us=video_total_;t.ui_tail_us=tail_total_;
        t.ui_outside_peak_us=std::exchange(outside_peak_,0);
    }
    void publish(const StreamHealthWindow& s) {
        const std::array<std::uint64_t,26> values={s.sample,s.duration_ms,
            static_cast<std::uint64_t>(s.ui_fps*1000),static_cast<std::uint64_t>(s.received_fps*1000),
            static_cast<std::uint64_t>(s.decoded_fps*1000),static_cast<std::uint64_t>(s.submitted_fps*1000),
            static_cast<std::uint64_t>(s.ui_peak_ms*1000),static_cast<std::uint64_t>(s.poll_recent_ms*1000),
            s.ui_slow,s.command_busy,s.upload_busy,s.hw_ref_fail,s.mapping_fail,s.hw_frames,
            session_peak_,stats_.command_busy,stats_.upload_busy,stats_.hardware_ref_failed,stats_.mapping_failed,
            s.decode_over16,s.decode_over33,static_cast<std::uint64_t>(s.decode_peak_ms*1000),
            static_cast<std::uint64_t>(s.ui_input_ms*1000),static_cast<std::uint64_t>(s.ui_video_ms*1000),
            static_cast<std::uint64_t>(s.ui_tail_ms*1000),static_cast<std::uint64_t>(s.ui_outside_peak_ms*1000)};
        ++revision_;
        for(size_t i=0;i<values.size();++i) values_[i].store(values[i]);
        ++revision_;
    }
    template<class Report> void report(Report callback) const {
        std::array<std::uint64_t,26> values{};bool coherent=false;
        for(int attempt=0;attempt<2;++attempt) {
            const auto before=revision_.load();if(before&1) continue;
            for(size_t i=0;i<values.size();++i) values[i]=values_[i].load();
            if(before==revision_.load()) {coherent=true;break;}
        }
        if(!coherent) {callback("DEBUG_WINDOW_SNAPSHOT_BUSY",1,0);return;}
        callback("DEBUG_WINDOW_SAMPLE_BEGIN",static_cast<int>(std::min<std::uint64_t>(values[0],INT_MAX)),0);
        for(size_t i=0;i<names_.size();++i)
            callback(names_[i],static_cast<int>(std::min<std::uint64_t>(values[i],INT_MAX)),0);
        callback("DEBUG_WINDOW_SAMPLE_END",static_cast<int>(std::min<std::uint64_t>(values[0],INT_MAX)),0);
    }
private:
    inline static constexpr std::array<const char*,26> names_={"DEBUG_WINDOW_SAMPLE","DEBUG_WINDOW_DURATION_MS",
        "DEBUG_WINDOW_UI_MILLIFPS","DEBUG_WINDOW_RECEIVE_MILLIFPS","DEBUG_WINDOW_DECODE_MILLIFPS",
        "DEBUG_WINDOW_SUBMIT_MILLIFPS","DEBUG_WINDOW_UI_MAX_US","DEBUG_WINDOW_POLL_MAX_US",
        "DEBUG_WINDOW_UI_OVER_50MS","DEBUG_WINDOW_COMMAND_BUSY","DEBUG_WINDOW_UPLOAD_BUSY",
        "DEBUG_WINDOW_HW_REF_FAILED","DEBUG_WINDOW_MAPPING_FAILED","DEBUG_WINDOW_HW_FRAMES",
        "DEBUG_UI_SESSION_MAX_US","DEBUG_COMMAND_BUSY_TOTAL","DEBUG_UPLOAD_BUSY_TOTAL",
        "DEBUG_HW_REF_FAILED_TOTAL","DEBUG_MAPPING_FAILED_TOTAL","DEBUG_DECODE_OVER_16MS","DEBUG_DECODE_OVER_33MS",
        "DEBUG_DECODE_RECENT_MAX_US","DEBUG_UI_INPUT_MEAN_US","DEBUG_UI_VIDEO_MEAN_US",
        "DEBUG_UI_TAIL_MEAN_US","DEBUG_UI_OUTSIDE_MAX_US"};
    std::array<std::atomic_uint64_t,26> values_{};
    std::atomic_uint64_t revision_{0};
    bool started_=false;std::int64_t last_=0;
    std::uint64_t frames_=0,peak_=0,session_peak_=0,slow_=0;
    VideoRenderStats stats_{};
    std::int64_t draw_start_=0,video_start_=0,tail_start_=0,draw_end_=0;
    std::uint64_t draws_=0,input_total_=0,video_total_=0,tail_total_=0,outside_peak_=0;
    std::uint64_t input_current_=0,video_current_=0;
    bool draw_ended_=false,draw_active_=false,video_active_=false,tail_active_=false;
};
}
