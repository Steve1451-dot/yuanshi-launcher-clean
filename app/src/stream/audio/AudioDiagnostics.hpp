#pragma once
#include "AudioPipeline.hpp"
#include <algorithm>
#include <limits>

namespace opennow::audio {
// Successful decoding is NOT playback proof. Fixed tags and scalars only.
template<class Report> void ReportOutputHealth(const AudioLevels& levels, Report&& report) {
    const auto emit = [&](const char* name, std::uint64_t value) {
        report(name, static_cast<int>(std::min<std::uint64_t>(
            value, std::numeric_limits<int>::max())), 0);
    };
    emit("AUDIO_OUTPUT_READY", levels.output_ready);
    emit("AUDIO_PAYLOAD_BYTES_RECEIVED", levels.payload_bytes_received);
    emit("AUDIO_OUTPUT_STARTED", levels.output_started);
    emit("AUDIO_OUTPUT_ERRORS", levels.output_errors);
    emit("AUDIO_OUTPUT_LAST_ERROR", levels.last_output_error);
    emit("AUDIO_SAMPLES_SUBMITTED", levels.samples_submitted);
    emit("AUDIO_SAMPLES_PLAYED", levels.samples_played);
    emit("AUDIO_OUTPUT_DROPS", levels.output_drops);
    emit("AUDIO_OUTPUT_UNDERRUNS", levels.underruns);
    emit("AUDIO_OUT_PEAK", levels.output_peak);
    emit("AUDIO_OUT_RMS", levels.output_rms);
}
}
