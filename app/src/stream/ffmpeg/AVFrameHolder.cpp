//
//  DiscoverManager.cpp
//  Moonlight
//
// Created by XITRIX on 31.03.2025.
//

#include "AVFrameHolder.hpp"
#include "VideoFrameTiming.hpp"

#include "borealis.hpp"

AVFrameQueue::AVFrameQueue() {}

AVFrameQueue::~AVFrameQueue() {
    cleanup();
}

AVFrameReference::~AVFrameReference() {av_frame_free(&current_);av_frame_free(&spare_);}
void AVFrameReference::reset() {
    if(current_) av_frame_unref(current_);
    if(spare_) av_frame_unref(spare_);
    generation_=0;
}
bool AVFrameReference::retain(const AVFrame* source,uint64_t generation) {
    if(current_ && generation_==generation) return true;
    if(!spare_) spare_=av_frame_alloc();
    if(!spare_) return false;
    av_frame_unref(spare_);
    if(av_frame_ref(spare_,source)<0) {av_frame_unref(spare_);return false;}
    std::swap(current_,spare_);generation_=generation;
    if(spare_) av_frame_unref(spare_);
    return true;
}

void AVFrameQueue::setLimit(size_t queue_limit) {
    std::lock_guard<std::mutex> lock(m_mutex);
    limit=std::max<size_t>(2,queue_limit);
    while(queue.size()>limit) {
        auto* frame=queue.front();queue.pop_front();generations.pop_front();
        av_frame_free(&frame);++framesDroppedStat;++overflowDroppedStat;
    }
}

namespace
{

AVFrame* referenceFrame(const AVFrame* source)
{
    if (!source)
        return nullptr;
    return av_frame_clone(source);
}

} // namespace

void AVFrameQueue::push(const AVFrame* item) {
    AVFrame* copy = referenceFrame(item);
    if (!copy) {
        brls::Logger::warning("Frame queue: failed to copy decoded frame");
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    queue.push_back(copy);
    generations.push_back(++nextGeneration);
    framesCopiedStat++;

    if (queue.size() > limit) {
        AVFrame* dropped = queue.front();
        queue.pop_front();
        generations.pop_front();
        av_frame_free(&dropped);
        framesDroppedStat++;
        overflowDroppedStat++;
    }
}

AVFrame* AVFrameQueue::pop(bool& reused, uint64_t& generation, int64_t target_pts) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return popLocked(reused,generation,target_pts);
}

AVFrame* AVFrameQueue::acquire(AVFrameReference& reference,bool& reused,uint64_t& generation,
    int64_t target_pts,opennow::video::NativePresentationClock* clock,int64_t now_us) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if(clock) {
        const auto* front=queue.empty() ? bufferFrame : queue.front();
        const auto* newest=queue.empty() ? bufferFrame : queue.back();
        if(front && newest && front->pts!=AV_NOPTS_VALUE && newest->pts!=AV_NOPTS_VALUE) {
            const auto before=clock->rebases();
            target_pts=clock->target(now_us,static_cast<uint32_t>(front->pts),
                static_cast<uint32_t>(newest->pts),queue.empty() ? bufferGeneration : generations.back(),epoch);
            clockRebasesStat+=clock->rebases()-before;
        } else clock->reset();
    }
    auto* selected=popLocked(reused,generation,target_pts);
    if(!selected) {reference.reset();return nullptr;}
    if(!reference.retain(selected,generation)) {++referenceFailuresStat;reused=true;}
    generation=reference.generation();
    return generation ? reference.frame() : nullptr;
}

AVFrame* AVFrameQueue::popLocked(bool& reused,uint64_t& generation,int64_t target_pts) {

    while (!queue.empty()) {
        auto decision = opennow::video::FrameDecision::Present;
        if (target_pts == AV_NOPTS_VALUE) {
            if (queue.size() > 1)
                decision = opennow::video::FrameDecision::DropSuperseded;
        } else if (queue.front()->pts != AV_NOPTS_VALUE) {
            const bool has_next = queue.size() > 1 && queue[1]->pts != AV_NOPTS_VALUE;
            decision = opennow::video::DecideFrame(
                static_cast<uint32_t>(queue.front()->pts), has_next,
                has_next ? static_cast<uint32_t>(queue[1]->pts) : 0,
                bufferFrame != nullptr, static_cast<uint32_t>(target_pts));
        }
        if (decision == opennow::video::FrameDecision::HoldPrevious) {
            fakeFrameUsedStat++;
            timingHoldStat++;
            reused = true;
            generation = bufferGeneration;
            return bufferFrame;
        }
        if (decision != opennow::video::FrameDecision::DropSuperseded)
            break;
        AVFrame* dropped = queue.front();
        queue.pop_front();
        generations.pop_front();
        av_frame_free(&dropped);
        framesDroppedStat++;
        timingDroppedStat++;
    }

    if (!queue.empty()) {
        if (bufferFrame)
            av_frame_free(&bufferFrame);

        AVFrame* item = queue.front();
        queue.pop_front();
        bufferGeneration = generations.front();
        generations.pop_front();
        bufferFrame = item;
        reused = false;
    } else {
        fakeFrameUsedStat++;
        reused = true;
    }

    generation = bufferGeneration;
    return bufferFrame;
}

size_t AVFrameQueue::size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return queue.size();
}

size_t AVFrameQueue::getFakeFrameUsage() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return fakeFrameUsedStat;
}

size_t AVFrameQueue::getFramesDropStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return framesDroppedStat;
}

size_t AVFrameQueue::getTimingDropStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return timingDroppedStat;
}

size_t AVFrameQueue::getOverflowDropStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return overflowDroppedStat;
}

size_t AVFrameQueue::getTimingHoldStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return timingHoldStat;
}

size_t AVFrameQueue::getReferenceFailureStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);return referenceFailuresStat;
}
size_t AVFrameQueue::getClockRebaseStat() const {
    std::lock_guard<std::mutex> lock(m_mutex);return clockRebasesStat;
}

void AVFrameQueue::cleanup() {
    std::lock_guard<std::mutex> lock(m_mutex);
    while (!queue.empty()) {
        AVFrame* frame = queue.front();
        queue.pop_front();
        av_frame_free(&frame);
    }
    while (!generations.empty())
        generations.pop_front();

    if (bufferFrame)
        av_frame_free(&bufferFrame);

    fakeFrameUsedStat = 0;
    framesDroppedStat = 0;
    timingDroppedStat = 0;
    overflowDroppedStat = 0;
    timingHoldStat = 0;
    referenceFailuresStat = 0;
    clockRebasesStat = 0;
    framesCopiedStat = 0;
    // Never alias an old renderer/lease generation across stream resets.
    ++epoch;
    bufferGeneration = 0;
    bufferFrame = nullptr;
}
