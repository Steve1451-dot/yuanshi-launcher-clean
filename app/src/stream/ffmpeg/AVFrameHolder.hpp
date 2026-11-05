#pragma once

#include "Singleton.hpp"
#include "NativePresentationClock.hpp"
#include <mutex>
#include <functional>
#include <deque>
#include <algorithm>

extern "C" {
#include <libavcodec/avcodec.h>
}

struct AVFrame;

// UI-owned reusable references. A producer cleanup cannot invalidate the
// callback's frame. Two AVFrame headers permit transactional ref failure;
// pixel data is referenced, never copied for this lease.
class AVFrameReference {
public:
    ~AVFrameReference();
    AVFrameReference()=default;
    AVFrameReference(const AVFrameReference&)=delete;
    AVFrameReference& operator=(const AVFrameReference&)=delete;
    bool retain(const AVFrame* source,uint64_t generation);
    void reset();
    AVFrame* frame() const {return current_;}
    uint64_t generation() const {return generation_;}
private:
    AVFrame* current_=nullptr;
    AVFrame* spare_=nullptr;
    uint64_t generation_=0;
};

class AVFrameQueue {
public:
    explicit AVFrameQueue();
    ~AVFrameQueue();

    void push(const AVFrame* item);
    AVFrame* pop(bool& reused, uint64_t& generation, int64_t target_pts = AV_NOPTS_VALUE);
    AVFrame* acquire(AVFrameReference& reference,bool& reused,uint64_t& generation,
        int64_t target_pts=AV_NOPTS_VALUE,
        opennow::video::NativePresentationClock* clock=nullptr,int64_t now_us=0);
    void setLimit(size_t queue_limit);

    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t getFakeFrameUsage() const;
    [[nodiscard]] size_t getFramesDropStat() const;
    [[nodiscard]] size_t getTimingDropStat() const;
    [[nodiscard]] size_t getOverflowDropStat() const;
    [[nodiscard]] size_t getTimingHoldStat() const;
    [[nodiscard]] size_t getReferenceFailureStat() const;
    [[nodiscard]] size_t getClockRebaseStat() const;

    void cleanup();

private:
    friend class AVFrameHolder;
    AVFrame* popLocked(bool& reused,uint64_t& generation,int64_t target_pts);
    size_t limit = 3;
    std::deque<AVFrame*> queue;
    AVFrame* bufferFrame = nullptr;
    mutable std::mutex m_mutex;
    size_t fakeFrameUsedStat = 0;
    size_t framesDroppedStat = 0;
    size_t timingDroppedStat = 0;
    size_t overflowDroppedStat = 0;
    size_t timingHoldStat = 0;
    size_t referenceFailuresStat = 0;
    size_t clockRebasesStat = 0;
    size_t framesCopiedStat = 0;
    uint64_t nextGeneration = 0;
    uint64_t bufferGeneration = 0;
    uint64_t epoch = 0;
    std::deque<uint64_t> generations;
};

class AVFrameHolder : public Singleton<AVFrameHolder> {
  public:
    void push(const AVFrame* frame) {
        m_frame_queue.push(frame);
    }

    void get(const std::function<void(AVFrame*, uint64_t, bool)>& fn,
             int64_t target_pts = AV_NOPTS_VALUE) {
        bool reused = false;
        uint64_t generation = 0;
        auto frame = m_frame_queue.acquire(view_reference_,reused,generation,target_pts);

        if (frame) {
            fn(frame, generation, reused);
        }
    }

    void get_paced(const std::function<void(AVFrame*,uint64_t,bool)>& fn,int64_t now_us) {
        bool reused=false;uint64_t generation=0;
        auto* frame=m_frame_queue.acquire(view_reference_,reused,generation,
            AV_NOPTS_VALUE,&presentation_clock_,now_us);
        if(frame) fn(frame,generation,reused);
    }

    // UI thread, after rendering has stopped. Producer cleanup does not touch
    // these references, including while a draw callback is in flight.
    void releaseViewReference() {view_reference_.reset();presentation_clock_.reset();}

    void prepare(size_t queue_limit = 3) {
        // Software frame threading emits short bursts, so one additional slot
        // prevents avoidable drops while adding at most one display interval.
        m_frame_queue.setLimit(queue_limit);
    }

    void cleanup() {
        m_frame_queue.cleanup();
    }

    [[nodiscard]] int getStat() const { return static_cast<int>(m_frame_queue.size()); }
    [[nodiscard]] size_t getFakeFrameStat() const { return m_frame_queue.getFakeFrameUsage(); }
    [[nodiscard]] size_t getFrameDropStat() const { return m_frame_queue.getFramesDropStat(); }
    [[nodiscard]] size_t getTimingDropStat() const { return m_frame_queue.getTimingDropStat(); }
    [[nodiscard]] size_t getOverflowDropStat() const { return m_frame_queue.getOverflowDropStat(); }
    [[nodiscard]] size_t getTimingHoldStat() const { return m_frame_queue.getTimingHoldStat(); }
    [[nodiscard]] size_t getFrameQueueSize() const { return m_frame_queue.size(); }
    [[nodiscard]] size_t getReferenceFailureStat() const {return m_frame_queue.getReferenceFailureStat();}
    [[nodiscard]] size_t getClockRebaseStat() const {return m_frame_queue.getClockRebaseStat();}

  private:
    AVFrameQueue m_frame_queue;
    AVFrameReference view_reference_;
    opennow::video::NativePresentationClock presentation_clock_;
};
