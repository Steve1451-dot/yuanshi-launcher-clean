#pragma once
#include "stream/audio/AudioPipeline.hpp"
#include "stream/ffmpeg/FFmpegVideoDecoder.hpp"
#include "webrtc/decode_recovery_state.hpp"
#include "stream/StageTimings.hpp"
#include "stream/FrameRecoveryTimings.hpp"
#include "stream/DecodeBufferPool.hpp"
#include <peer_connection.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace cloudgenshin {
// Network callbacks copy access units; one decoder worker submits them. UI
// consumes AVFrameHolder through IVideoRenderer, never Deko3D from this class.
class MediaPipeline {
public:
    ~MediaPipeline();
    bool start(int fps,int audio_buffer_ms=40,bool hardware_probe=false);
    void set_fps(int fps) {fps_=fps==30 ? 30 : 60;}
    // Stop accepting decode work without freeing a frame borrowed by the UI.
    // The owner joins the decoder and frees frames via stop() on the UI thread.
    void request_stop();
    void stop();
    void video(const PeerVideoPacket& packet);
    void audio(const PeerAudioPacket& packet) { if (audio_started_) audio_.submit(packet); }
    bool take_keyframe_request(std::uint64_t* generation=nullptr);
    void request_resync();
    // The RTP depacketizer has already exhausted its repair window and
    // destroyed one complete access unit. Gate dependent pictures now instead
    // of waiting for the multi-second decode-progress watchdog. Complete units
    // queued before the damage stay decodable; no corrupt bytes were submitted.
    void notify_video_damage();
    unsigned decoded() const { return decoded_.load(); }
    unsigned drops() const { return drops_.load(); }
    unsigned errors() const { return errors_.load(); }
    unsigned idrs() const { return idrs_.load(); }
    unsigned submitted() const {return submitted_.load();}
    unsigned zero_output() const {return zero_output_.load();}
    unsigned queue_depth() const {return queue_depth_.load();}
    // Size buckets of the video access units the pod actually sent. A frozen
    // picture with a steady trickle of tiny units is a pod sending SEI/filler,
    // not a decoder that lost its reference frame.
    unsigned last_unit_bytes() const {return last_unit_bytes_.load();}
    unsigned last_unit_idr() const {return last_unit_idr_.load();}
    unsigned small_units() const {return small_units_.load();}
    unsigned medium_units() const {return medium_units_.load();}
    unsigned large_units() const {return large_units_.load();}
    unsigned corrupt_frames() const {return corrupt_frames_.load();}
    int last_decode_error() const { return last_decode_error_.load(); }
    bool hardware_decoder() const { return hardware_active_.load(); }
    bool sound_ready() const { return audio_started_; }
    AudioLevels audio_levels() const { return audio_.levels(); }
    struct Performance {
        unsigned congestion_events,congestion_drops,waiting_idr_drops,resync_events,resync_drops,stale_drops,hardware_frames;
        std::uint64_t decode_mean_us,decode_max_us,queue_wait_mean_us,queue_wait_max_us;
        int hardware_setup_stage,hardware_setup_error,hardware_runtime_error;
        unsigned hardware_fallbacks;
        unsigned buffer_allocations=0,buffer_reuses=0,buffer_failures=0,buffer_reserved=0;
        unsigned video_damage_events=0;
        opennow::video::FrameRecoveryTimings::Snapshot recovery;
    };
    Performance performance() const;
    opennow::video::StageTimings::Snapshot decode_timing() const {return decode_timings_.snapshot();}
    opennow::video::StageTimings::Snapshot queue_timing() const {return queue_timings_.snapshot();}
    std::uint64_t take_decode_peak() {return decode_timings_.takeIntervalMaximum();}
private:
    struct Unit {
        std::vector<std::uint8_t> bytes;
        std::uint32_t timestamp;
        std::uint64_t generation;
        std::chrono::steady_clock::time_point queued;
        std::uint64_t recovery_ticket=0;
    };
    void decode_loop();
    void clear_queue_locked();
    void begin_resync_locked();
    void recycle_locked(std::vector<std::uint8_t>&& bytes);
    FFmpegVideoDecoder decoder_;
    AudioPipeline audio_;
    bool audio_started_ = false;
    std::atomic_bool running_{false};
    std::atomic_uint decoded_{0}, drops_{0}, errors_{0}, idrs_{0};
    std::atomic_uint submitted_{0},zero_output_{0},queue_depth_{0};
    std::atomic_uint last_unit_bytes_{0},last_unit_idr_{0};
    std::atomic_uint small_units_{0},medium_units_{0},large_units_{0};
    std::atomic_uint corrupt_frames_{0};
    std::atomic_int last_decode_error_{0};
    std::atomic_uint congestion_events_{0},congestion_drops_{0},waiting_idr_drops_{0};
    std::atomic_uint resync_events_{0},resync_drops_{0},stale_drops_{0},hardware_frames_{0};
    std::atomic_uint video_damage_events_{0};
    opennow::video::StageTimings decode_timings_,queue_timings_;
    int hardware_setup_stage_=0,hardware_setup_error_=0;
    std::atomic_bool hardware_active_{false};
    std::atomic_int hardware_runtime_error_{0};
    std::atomic_uint hardware_fallbacks_{0};
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Unit> queue_;
    opennow::video::DecodeBufferPool buffer_pool_;
    std::atomic_uint buffer_allocations_{0},buffer_reuses_{0},buffer_failures_{0},buffer_reserved_{0};
    opennow::webrtc::DecodeRecoveryState recovery_;
    opennow::video::FrameRecoveryTimings recovery_timings_;
    std::atomic_int fps_{30};
};
} // namespace cloudgenshin
