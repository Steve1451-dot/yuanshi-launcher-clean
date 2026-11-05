#pragma once
#include "media_pipeline.hpp"
#include <algorithm>
#include <climits>
namespace cloudgenshin {
template<class Report> void ReportMediaPerformance(const MediaPipeline::Performance& p,Report&& report) {
    const auto emit=[&](const char* name,std::uint64_t value) {report(name,static_cast<int>(std::min<std::uint64_t>(value,INT_MAX)),0);};
    report("VIDEO_HW_SETUP_STAGE",p.hardware_setup_stage,0);
    report("VIDEO_HW_SETUP_ERROR",p.hardware_setup_error,0);
    report("VIDEO_HW_RUNTIME_ERROR",p.hardware_runtime_error,0);
    emit("VIDEO_HW_FALLBACKS",p.hardware_fallbacks);
    emit("VIDEO_HARDWARE_FRAMES",p.hardware_frames);
    emit("VIDEO_CONGESTION_EVENTS",p.congestion_events);
    emit("VIDEO_CONGESTION_DROPS",p.congestion_drops);
    emit("VIDEO_WAITING_IDR_DROPS",p.waiting_idr_drops);
    emit("VIDEO_RESYNC_EVENTS",p.resync_events);
    emit("VIDEO_RESYNC_QUEUE_DROPS",p.resync_drops);
    emit("VIDEO_STALE_GENERATION_DROPS",p.stale_drops);
    emit("VIDEO_RTP_DAMAGE_EVENTS",p.video_damage_events);
    emit("VIDEO_RECOVERY_EPISODES",p.recovery.episodes);
    emit("VIDEO_RECOVERY_COMPLETED",p.recovery.completed);
    emit("VIDEO_RECOVERY_SUPERSEDED",p.recovery.superseded);
    emit("VIDEO_RECOVERY_PHASE",p.recovery.phase);
    emit("VIDEO_RECOVERY_IDR_LAST_US",p.recovery.idr_last_us);
    emit("VIDEO_RECOVERY_IDR_MAX_US",p.recovery.idr_max_us);
    emit("VIDEO_RECOVERY_OUTPUT_LAST_US",p.recovery.output_last_us);
    emit("VIDEO_RECOVERY_OUTPUT_MAX_US",p.recovery.output_max_us);
    emit("VIDEO_RECOVERY_TOTAL_LAST_US",p.recovery.total_last_us);
    emit("VIDEO_RECOVERY_TOTAL_MAX_US",p.recovery.total_max_us);
    emit("VIDEO_DECODE_MEAN_US",p.decode_mean_us);
    emit("VIDEO_DECODE_MAX_US",p.decode_max_us);
    emit("VIDEO_QUEUE_WAIT_MEAN_US",p.queue_wait_mean_us);
    emit("VIDEO_QUEUE_WAIT_MAX_US",p.queue_wait_max_us);
    emit("VIDEO_BUFFER_ALLOCATIONS",p.buffer_allocations);
    emit("VIDEO_BUFFER_REUSES",p.buffer_reuses);
    emit("VIDEO_BUFFER_ALLOCATION_FAILURES",p.buffer_failures);
    emit("VIDEO_BUFFER_POOL_BYTES",p.buffer_reserved);
}
}
