#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

struct PeerAudioPacket;

// Coarse decoder/output health for the on-screen diagnostics. Levels are the
// last decoded block's peak/RMS in 16-bit sample units: a pod that streams a
// live game has non-zero audio, a frozen desktop is silent.
struct AudioLevels {
    std::uint64_t packets_received = 0;
    std::uint64_t payload_bytes_received = 0;
    std::uint64_t packets_decoded = 0;
    std::uint64_t decode_errors = 0;
    std::uint64_t samples_decoded = 0;
    int input_peak = 0;
    int input_rms = 0;
    int output_peak = 0;
    int output_rms = 0;
    bool output_ready = false;
    bool output_started = false;
    std::uint32_t last_output_error = 0;
    std::uint64_t output_errors = 0;
    std::uint64_t samples_submitted = 0;
    std::uint64_t samples_played = 0;
    std::uint64_t output_drops = 0;
    std::uint64_t underruns = 0;
};

class AudioPipeline {
public:
    AudioPipeline();
    ~AudioPipeline();

    bool start();
    void configure(int volume_percent, int target_buffer_ms);
    void stop();
    void submit(const PeerAudioPacket& packet);
    void set_sender_report(uint32_t ssrc, uint64_t ntp_us, uint32_t rtp_timestamp);
    std::string debug_info() const;
    AudioLevels levels() const;
    int64_t playback_ntp_us() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
