#pragma once
#include <atomic>
#include <cstdint>
#include <optional>

namespace cloudgenshin::overlay {
// The network owner samples libpeer's existing smoothed ICE/SCTP RTT, never
// the decoder/renderer duration or pre-launch node ping. The UI only reads a
// small atomic snapshot; it must not query a peer or take the SCTP mutex.
class NetworkLatency {
public:
    static constexpr std::int64_t kSampleIntervalMs=1000,kStaleMs=5000;
    template<class Reader> void sample(std::int64_t now_ms,Reader read) {
        if(now_ms<0 || static_cast<std::uint64_t>(now_ms)>kMaximumTime) return;
        if(next_sample_ms_>=0 && now_ms>=last_sample_ms_ && now_ms<next_sample_ms_) return;
        last_sample_ms_=now_ms;next_sample_ms_=now_ms+kSampleIntervalMs;
        const int rtt=read();
        if(rtt<0 || rtt>60000) {clear();return;}
        // Zero RTT is a valid sub-millisecond sample; zero packed value is
        // reserved for unknown. A single publication avoids torn timestamps.
        snapshot_.store((static_cast<std::uint64_t>(now_ms)<<16)|static_cast<unsigned>(rtt+1),
            std::memory_order_release);
    }
    std::optional<int> value(std::int64_t now_ms) const {
        const auto packed=snapshot_.load(std::memory_order_acquire);
        if(!packed || now_ms<0) return {};
        const auto sampled=static_cast<std::int64_t>(packed>>16);
        if(now_ms<sampled || now_ms-sampled>kStaleMs) return {};
        return static_cast<int>(packed&0xffff)-1;
    }
    void clear() {snapshot_.store(0,std::memory_order_release);}
private:
    static constexpr std::uint64_t kMaximumTime=(UINT64_MAX>>16)-kSampleIntervalMs;
    std::int64_t next_sample_ms_=-1,last_sample_ms_=0; // Network-owner only.
    std::atomic_uint64_t snapshot_{0};
};
} // namespace cloudgenshin::overlay
