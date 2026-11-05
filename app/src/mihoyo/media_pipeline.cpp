#include "mihoyo/media_pipeline.hpp"
#include "mihoyo/stream_diagnostics_policy.hpp"
#include "stream/DecodeQueuePolicy.hpp"
#include "stream/ffmpeg/HardwareDecodePolicy.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "webrtc/internal.hpp"
#include <new>

namespace cloudgenshin {
namespace {
std::uint64_t RecoveryMicroseconds(std::chrono::steady_clock::time_point now=std::chrono::steady_clock::now()) {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
}
}
MediaPipeline::~MediaPipeline() { stop(); }
bool MediaPipeline::start(int fps,int audio_buffer_ms,bool hardware_probe) {
    stop(); fps_=EffectiveStreamFps(fps);
    int result=decoder_.setup(VIDEO_FORMAT_H264,1280,720,fps_,nullptr,
        hardware_probe ? VIDEO_DECODER_PREFER_HARDWARE : VIDEO_DECODER_FORCE_SOFTWARE);
    hardware_setup_stage_=hardware_probe ? decoder_.setup_stage() : 0;
    hardware_setup_error_=hardware_probe ? result : 0;
    if (hardware_probe && result != 0) {
        decoder_.cleanup();
        result=decoder_.setup(VIDEO_FORMAT_H264,1280,720,fps_,nullptr,VIDEO_DECODER_FORCE_SOFTWARE);
    }
    if (result != 0) {decoder_.cleanup();return false;}
    hardware_active_=decoder_.uses_hardware_frames();hardware_runtime_error_=0;hardware_fallbacks_=0;
    AVFrameHolder::instance().prepare(3);
    audio_.configure(100,audio_buffer_ms);
    audio_started_=audio_.start();
    decoded_=0; drops_=0; errors_=0; idrs_=0;
    buffer_allocations_=0;buffer_reuses_=0;buffer_failures_=0;buffer_reserved_=0;
    submitted_=0;zero_output_=0;queue_depth_=0;
    last_unit_bytes_=0;last_unit_idr_=0;
    small_units_=0;medium_units_=0;large_units_=0;
    corrupt_frames_=0;
    last_decode_error_=0;
    congestion_events_=0;congestion_drops_=0;waiting_idr_drops_=0;
    resync_events_=0;resync_drops_=0;stale_drops_=0;hardware_frames_=0;
    video_damage_events_=0;
    recovery_timings_.reset();
    decode_timings_.reset();queue_timings_.reset();
    recovery_={}; recovery_.require_resync();
    running_=true;
    worker_=std::thread([this]{decode_loop();});return true;
}
void MediaPipeline::request_stop() {
    // Update the wait predicate under the same lock as decode_loop's wait.
    // Atomic alone does not prevent notify from landing between the predicate
    // check and the waiter going to sleep, leaving stop() stuck in join().
    {std::lock_guard lock(mutex_);running_=false;}
    wake_.notify_all();
}
void MediaPipeline::stop() {
    request_stop();
    if (worker_.joinable()) worker_.join();
    {std::lock_guard lock(mutex_);queue_.clear();buffer_pool_.clear();buffer_reserved_=0;queue_depth_=0;}
    audio_.stop(); audio_started_=false;
    decoder_.cleanup(); AVFrameHolder::instance().cleanup();
}
void MediaPipeline::video(const PeerVideoPacket& packet) {
    if (!running_ || !packet.data || !packet.size ||
        packet.size>opennow::video::MaximumDecodeUnitBytes()) {++drops_;return;}
    const bool idr=opennow::webrtc::internal::ContainsH264Idr(packet.data,packet.size);
    if (idr) ++idrs_;
    last_unit_bytes_=static_cast<unsigned>(std::min<std::size_t>(packet.size,999999));
    last_unit_idr_=idr?1:0;
    if (packet.size<256) ++small_units_;
    else if (packet.size<2048) ++medium_units_;
    else ++large_units_;
    std::lock_guard lock(mutex_);
    const auto now=std::chrono::steady_clock::now();
    if (queue_.size()>=opennow::video::MaximumQueuedAccessUnits(fps_) ||
        (!queue_.empty() && now-queue_.front().queued>=std::chrono::milliseconds(opennow::video::MaximumDecodeQueueDelayMs()))) {
        ++congestion_events_;congestion_drops_.fetch_add(static_cast<unsigned>(queue_.size()));
        drops_.fetch_add(static_cast<unsigned>(queue_.size()));clear_queue_locked();begin_resync_locked();
    }
    const auto generation=recovery_.admit(idr);
    if (!generation) {++drops_;++waiting_idr_drops_;return;}
    try {
        bool reused=false;auto bytes=buffer_pool_.take(packet.size,reused);
        if(reused) ++buffer_reuses_;else ++buffer_allocations_;
        bytes.assign(packet.data,packet.data+packet.size);
        queue_.push_back({std::move(bytes),packet.timestamp,*generation,now});
        queue_.back().recovery_ticket=recovery_timings_.admit(idr,RecoveryMicroseconds(now));
    } catch(const std::bad_alloc&) {
        ++buffer_failures_;drops_.fetch_add(static_cast<unsigned>(queue_.size()+1));
        clear_queue_locked();begin_resync_locked();return;
    }
    buffer_reserved_=static_cast<unsigned>(buffer_pool_.bytes());
    queue_depth_=static_cast<unsigned>(queue_.size());
    wake_.notify_one();
}
bool MediaPipeline::take_keyframe_request(std::uint64_t* generation) {
    std::lock_guard lock(mutex_);
    if(generation) *generation=recovery_.generation();
    // The owner rate-limits requests. Keep retrying until an IDR actually
    // arrives; a single lost/ignored request must not leave resync stuck.
    return recovery_.waiting_for_idr() || recovery_.take_keyframe_request();
}
void MediaPipeline::request_resync() {
    std::lock_guard lock(mutex_);
    ++resync_events_;resync_drops_.fetch_add(static_cast<unsigned>(queue_.size()));
    drops_.fetch_add(static_cast<unsigned>(queue_.size()));clear_queue_locked();
    begin_resync_locked();
}
void MediaPipeline::begin_resync_locked() {
    recovery_timings_.begin(RecoveryMicroseconds());
    recovery_.require_resync();
}
void MediaPipeline::notify_video_damage() {
    if (!running_) return;
    std::lock_guard lock(mutex_);
    ++video_damage_events_;
    // Deliberately does not clear the queue or advance the recovery
    // generation: frames that completed before this damage are still valid.
    recovery_.mark_damaged();
    recovery_timings_.begin(RecoveryMicroseconds());
}
void MediaPipeline::decode_loop() {
    while (running_) {
        Unit unit; bool reset=false;
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock,[this]{return !running_ || !queue_.empty();});
            if (!running_) break;
            unit=std::move(queue_.front());queue_.pop_front();
            queue_depth_=static_cast<unsigned>(queue_.size());
            if (!recovery_.is_current(unit.generation)) {++drops_;++stale_drops_;recycle_locked(std::move(unit.bytes));continue;}
            reset=recovery_.take_decoder_reset();
        }
        if (reset && !decoder_.uses_hardware_frames()) decoder_.reset_stream();
        const auto decode_started=std::chrono::steady_clock::now();
        queue_timings_.observe(std::chrono::duration_cast<std::chrono::microseconds>(decode_started-unit.queued).count());
        ++submitted_;
        const auto result=decoder_.submit_decode_unit(unit.bytes.data(),static_cast<int>(unit.bytes.size()),unit.timestamp);
        decode_timings_.observe(std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now()-decode_started).count());
        if(opennow::video::InitialHardwareFallbackAllowed(decoder_.uses_hardware_frames(),
            decoder_.has_output_frames(),hardware_fallbacks_.load(),result)) {
            // Some systems create a device but reject the first SPS/decode.
            // No frame was EVER published, so neither UI nor GPU can own one.
            // Keep networking alive, rebuild only the decoder and await IDR.
            hardware_runtime_error_=result;++hardware_fallbacks_;
            decoder_.cleanup();
            const int fallback=decoder_.setup(VIDEO_FORMAT_H264,1280,720,fps_,nullptr,VIDEO_DECODER_FORCE_SOFTWARE);
            hardware_active_=false;
            if(fallback!=0) {++errors_;last_decode_error_=fallback;running_=false;break;}
            std::lock_guard lock(mutex_);
            drops_.fetch_add(static_cast<unsigned>(queue_.size()+1));
            resync_drops_.fetch_add(static_cast<unsigned>(queue_.size()));
            clear_queue_locked();recycle_locked(std::move(unit.bytes));begin_resync_locked();
            continue;
        }
        corrupt_frames_=static_cast<unsigned>(decoder_.corrupt_frames_dropped());
        if (result>0) decoded_.fetch_add(static_cast<unsigned>(result));
        if (result>0 && decoder_.uses_hardware_frames()) hardware_frames_.fetch_add(static_cast<unsigned>(result));
        if (result==0) ++zero_output_;
        if (result<0) {++errors_;last_decode_error_=result;}
        std::lock_guard lock(mutex_);
        recycle_locked(std::move(unit.bytes));
        const auto completion=recovery_.complete(unit.generation,result>=0);
        if (completion==opennow::webrtc::DecodeRecoveryState::Completion::Failed) {
            recovery_timings_.begin(RecoveryMicroseconds());
            drops_.fetch_add(static_cast<unsigned>(queue_.size()));clear_queue_locked();
        } else if (completion==opennow::webrtc::DecodeRecoveryState::Completion::Accepted) {
            recovery_timings_.output(unit.recovery_ticket,result>0,RecoveryMicroseconds());
        }
    }
}
void MediaPipeline::recycle_locked(std::vector<std::uint8_t>&& bytes) {
    buffer_pool_.recycle(std::move(bytes));buffer_reserved_=static_cast<unsigned>(buffer_pool_.bytes());
}
void MediaPipeline::clear_queue_locked() {
    while(!queue_.empty()) {recycle_locked(std::move(queue_.front().bytes));queue_.pop_front();}
    queue_depth_=0;buffer_reserved_=static_cast<unsigned>(buffer_pool_.bytes());
}
MediaPipeline::Performance MediaPipeline::performance() const {
    return {congestion_events_.load(),congestion_drops_.load(),waiting_idr_drops_.load(),resync_events_.load(),
        resync_drops_.load(),stale_drops_.load(),hardware_frames_.load(),
        decode_timings_.mean(),decode_timings_.maximum(),queue_timings_.mean(),queue_timings_.maximum(),
        hardware_setup_stage_,hardware_setup_error_,hardware_runtime_error_.load(),hardware_fallbacks_.load(),
        buffer_allocations_.load(),buffer_reuses_.load(),buffer_failures_.load(),buffer_reserved_.load(),
        video_damage_events_.load(),recovery_timings_.snapshot()};
}
} // namespace cloudgenshin
