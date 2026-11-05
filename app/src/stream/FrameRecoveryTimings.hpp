#pragma once

#include <atomic>
#include <cstdint>

namespace opennow::video {
// State changes share the MediaPipeline queue lock. Snapshots are approximate
// atomic aggregates, like StageTimings: UI/logging never takes the queue lock.
// A ticket prevents an older in-flight decode from closing a newer recovery.
class FrameRecoveryTimings {
public:
    struct Snapshot {
        std::uint64_t episodes=0,completed=0,superseded=0;
        std::uint64_t idr_last_us=0,idr_max_us=0;
        std::uint64_t output_last_us=0,output_max_us=0;
        std::uint64_t total_last_us=0,total_max_us=0;
        unsigned phase=0; // 0: idle, 1: awaiting IDR, 2: awaiting decode output.
    };
    void reset() {
        ticket_=0;started_us_=idr_us_=0;phase_=0;
        episodes_=0;completed_=0;superseded_=0;
        idr_last_=0;idr_max_=0;output_last_=0;output_max_=0;
        total_last_=0;total_max_=0;published_phase_=0;
    }
    void begin(std::uint64_t now_us) {
        // Repeated lost deltas must not restart the clock indefinitely.
        if(phase_==1) return;
        if(phase_==2) superseded_.fetch_add(1,std::memory_order_relaxed);
        if(++ticket_==0) ++ticket_; // Zero denotes an unrelated decode unit.
        started_us_=now_us;idr_us_=0;phase_=1;
        episodes_.fetch_add(1,std::memory_order_relaxed);
        published_phase_.store(phase_,std::memory_order_relaxed);
    }
    // Called only after a complete AU has actually been queued successfully.
    std::uint64_t admit(bool idr,std::uint64_t now_us) {
        if(phase_==1 && idr) {
            idr_us_=now_us;phase_=2;
            record(idr_last_,idr_max_,elapsed(now_us,started_us_));
            published_phase_.store(phase_,std::memory_order_relaxed);
        }
        return phase_==2 ? ticket_ : 0;
    }
    // Zero-output decoder submissions are not recovery. A later complete AU
    // on the same ticket may produce the first output (e.g. codec buffering).
    void output(std::uint64_t ticket,bool produced_frame,std::uint64_t now_us) {
        if(!produced_frame || phase_!=2 || ticket==0 || ticket!=ticket_) return;
        record(output_last_,output_max_,elapsed(now_us,idr_us_));
        record(total_last_,total_max_,elapsed(now_us,started_us_));
        completed_.fetch_add(1,std::memory_order_relaxed);
        phase_=0;published_phase_.store(phase_,std::memory_order_relaxed);
    }
    Snapshot snapshot() const {
        return {episodes_.load(std::memory_order_relaxed),completed_.load(std::memory_order_relaxed),
            superseded_.load(std::memory_order_relaxed),idr_last_.load(std::memory_order_relaxed),
            idr_max_.load(std::memory_order_relaxed),output_last_.load(std::memory_order_relaxed),
            output_max_.load(std::memory_order_relaxed),total_last_.load(std::memory_order_relaxed),
            total_max_.load(std::memory_order_relaxed),published_phase_.load(std::memory_order_relaxed)};
    }
private:
    static std::uint64_t elapsed(std::uint64_t now,std::uint64_t start) {return now>=start ? now-start : 0;}
    static void record(std::atomic_uint64_t& last,std::atomic_uint64_t& maximum,std::uint64_t value) {
        last.store(value,std::memory_order_relaxed);
        // Writers are serialized by the queue lock; no CAS loop is needed.
        if(value>maximum.load(std::memory_order_relaxed)) maximum.store(value,std::memory_order_relaxed);
    }
    std::uint64_t ticket_=0,started_us_=0,idr_us_=0;
    unsigned phase_=0;
    std::atomic_uint64_t episodes_{0},completed_{0},superseded_{0};
    std::atomic_uint64_t idr_last_{0},idr_max_{0},output_last_{0},output_max_{0},total_last_{0},total_max_{0};
    std::atomic_uint published_phase_{0};
};
} // namespace opennow::video
