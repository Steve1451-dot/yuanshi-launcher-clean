#pragma once
#include "mihoyo/stream_health.hpp"
#include "mihoyo/media_pipeline.hpp"
#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "mihoyo/presentation_diagnostics.hpp"
namespace cloudgenshin::overlay {
inline StreamHealthTotals HealthTotals(MediaPipeline& media,std::uint64_t received,
    std::uint64_t submitted,std::uint64_t bytes,
    opennow::video::StageTimings::Snapshot render,std::uint64_t poll_peak_us,
    PresentationDiagnostics* debug=nullptr,std::uint64_t poll_recent=0) {
    const auto decode=media.decode_timing(),queue=media.queue_timing();
    const auto p=media.performance();const auto a=media.audio_levels();
    StreamHealthTotals result{received,media.decoded(),submitted,bytes,decode.count,decode.total,queue.count,queue.total,
        p.waiting_idr_drops,p.congestion_drops,AVFrameHolder::instance().getFrameDropStat(),
        a.samples_played,a.output_drops,a.underruns,render.count,render.total,poll_peak_us};
    if(debug) debug->append(result,poll_recent,p.hardware_frames);
    result.decode_over16=decode.over_16ms;result.decode_over33=decode.over_33ms;
    result.decode_peak_us=media.take_decode_peak();
    return result;
}
}
