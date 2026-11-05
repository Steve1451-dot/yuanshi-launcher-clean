#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace cloudgenshin {
// A single dirty bit, not a queue of snapshots. The log itself owns its
// bounded event history. RTC callbacks must not flush/rename an SD file for
// every diagnostic or keyframe request while UDP packets are arriving.
class SafeLogWriter {
public:
    explicit SafeLogWriter(std::function<void()> save) : save_(std::move(save)) {
        try {worker_=std::thread([this]{run();});} catch(...) {} // Caller can fall back.
    }
    SafeLogWriter(const SafeLogWriter&)=delete;
    SafeLogWriter& operator=(const SafeLogWriter&)=delete;
    ~SafeLogWriter() {
        {std::lock_guard lock(mutex_);stopping_=true;}
        changed_.notify_one();
        if(worker_.joinable()) worker_.join(); // Flush the final dirty snapshot.
    }
    bool request() {
        if(!worker_.joinable()) return false;
        {std::lock_guard lock(mutex_);dirty_=true;}
        changed_.notify_one();
        return true;
    }
private:
    void run() {
        using Clock=std::chrono::steady_clock;
        auto next=Clock::now();
        std::unique_lock lock(mutex_);
        while(true) {
            changed_.wait(lock,[this]{return dirty_ || stopping_;});
            if(!dirty_ && stopping_) break;
            changed_.wait_until(lock,next,[this]{return stopping_;});
            dirty_=false;
            lock.unlock();
            try {save_();} catch(...) {} // Logging cannot terminate gameplay.
            lock.lock();
            // Coalesce autosaves to avoid competing with UDP for SD/CPU time.
            // Manual exports remain immediate; shutdown flushes pending data.
            next=Clock::now()+std::chrono::seconds(2);
        }
    }
    std::function<void()> save_;
    std::mutex mutex_;
    std::condition_variable changed_;
    bool dirty_=false,stopping_=false;
    std::thread worker_;
};
} // namespace cloudgenshin
