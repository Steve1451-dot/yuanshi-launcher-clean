#pragma once
#include <atomic>
#include <cstdint>

namespace opennow::video {
// Cheap bounded aggregate, no samples/logging/allocations on a media thread.
class StageTimings {
public:
    struct Snapshot {std::uint64_t count,total,over_16ms=0,over_33ms=0;};
    Snapshot snapshot() const {return {count_.load(std::memory_order_relaxed),total_.load(std::memory_order_relaxed),over_16ms_.load(),over_33ms_.load()};}
    void reset() {count_=0;total_=0;maximum_=0;interval_maximum_=0;over_16ms_=0;over_33ms_=0;}
    std::uint64_t takeIntervalMaximum() {return interval_maximum_.exchange(0,std::memory_order_relaxed);}
    void observe(std::int64_t microseconds) {
        const auto value=static_cast<std::uint64_t>(microseconds>0 ? microseconds : 0);
        total_.fetch_add(value,std::memory_order_relaxed);
        count_.fetch_add(1,std::memory_order_relaxed);
        if(value>16667) over_16ms_.fetch_add(1,std::memory_order_relaxed);
        if(value>33334) over_33ms_.fetch_add(1,std::memory_order_relaxed);
        auto previous=maximum_.load(std::memory_order_relaxed);
        while(value>previous && !maximum_.compare_exchange_weak(previous,value,std::memory_order_relaxed)) {}
        previous=interval_maximum_.load(std::memory_order_relaxed);
        while(value>previous && !interval_maximum_.compare_exchange_weak(previous,value,std::memory_order_relaxed)) {}
    }
    std::uint64_t mean() const {
        const auto count=count_.load(std::memory_order_relaxed);
        return count ? total_.load(std::memory_order_relaxed)/count : 0;
    }
    std::uint64_t maximum() const {return maximum_.load(std::memory_order_relaxed);}
private:
    std::atomic_uint64_t count_{0},total_{0},maximum_{0},interval_maximum_{0};
    std::atomic_uint64_t over_16ms_{0},over_33ms_{0};
};
}
