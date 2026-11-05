#include "stream/ffmpeg/AVFrameHolder.hpp"
#include "ffmpeg_reference_model.hpp"
#include <cassert>
#include <thread>
static void push(int64_t pts) {
    auto* frame=av_frame_alloc();frame->format=AV_PIX_FMT_YUV420P;
    frame->width=16;frame->height=16;frame->pts=pts;
    assert(av_frame_get_buffer(frame,0)==0);
    frame->data[0][0]=static_cast<uint8_t>(pts);
    AVFrameHolder::instance().push(frame);av_frame_free(&frame);
}
int main() {
    auto& holder=AVFrameHolder::instance();holder.cleanup();push(90000);
    holder.get([&](AVFrame* frame,uint64_t,bool) {
        holder.cleanup(); // A concurrent producer reset has the same lifetime effect.
        assert(frame_model::live_buffers.load()==1 && "cleanup released the frame still borrowed by draw");
        assert(frame->pts==90000);
    });
    holder.get([](AVFrame*,uint64_t,bool) {assert(false);});
    assert(frame_model::live_buffers.load()==0);

    push(90000);push(93000);
    holder.get_paced([](AVFrame* f,uint64_t,bool reused) {assert(f->pts==90000 && !reused);},0);
    holder.get_paced([](AVFrame* f,uint64_t,bool reused) {assert(f->pts==90000 && reused);},16667);
    holder.get_paced([](AVFrame* f,uint64_t,bool reused) {assert(f->pts==93000 && !reused);},33334);
    assert(holder.getFrameDropStat()==0 && holder.getTimingHoldStat()==1);
    const auto allocated=frame_model::frame_allocations.load();
    for(int i=0;i<100;++i) holder.get_paced([](AVFrame* f,uint64_t,bool) {assert(f->pts==93000);},40000+i*100);
    assert(frame_model::frame_allocations.load()==allocated); // No per-refresh clone allocation.
    push(96000);frame_model::fail_next_ref=true;
    uint64_t old_generation=0;
    holder.get([&](AVFrame* f,uint64_t g,bool reused) {assert(f->pts==93000 && reused);old_generation=g;});
    assert(holder.getReferenceFailureStat()==1);
    holder.get([&](AVFrame* f,uint64_t g,bool) {assert(f->pts==96000 && g>old_generation);old_generation=g;});
    holder.cleanup();push(1000);
    holder.get_paced([&](AVFrame* f,uint64_t g,bool) {assert(f->pts==1000 && g>old_generation);},50000);
    holder.cleanup();push(AV_NOPTS_VALUE);push(AV_NOPTS_VALUE);
    holder.get_paced([](AVFrame* f,uint64_t,bool) {assert(f->pts==AV_NOPTS_VALUE);},60000);
    holder.cleanup();holder.releaseViewReference();assert(frame_model::live_buffers.load()==0);

    // Decode-thread bursts of two 60 fps frames retain their separate slots
    // at a 60 Hz UI, rather than always discarding the first of each pair.
    for(int i=0;i<600;++i) {
        if(i%2==0) {push(90000+i*1500);push(90000+(i+1)*1500);}
        holder.get_paced([i](AVFrame* f,uint64_t,bool reused) {
            assert(f->pts==90000+i*1500 && !reused);
        },static_cast<int64_t>(i)*1000000/60);
        assert(holder.getFrameQueueSize()<=1);
    }
    assert(holder.getFrameDropStat()==0 && holder.getClockRebaseStat()==0);
    // UI suspension catches up once, without replaying stale frames.
    push(1000000);push(1001500);push(1003000);
    holder.get_paced([](AVFrame* f,uint64_t,bool) {assert(f->pts==1003000);},12000000);
    assert(holder.getFrameDropStat()==2 && holder.getClockRebaseStat()==1);
    // Cleanup with no new source invalidates the UI lease on next acquire.
    holder.cleanup();
    holder.get_paced([](AVFrame*,uint64_t,bool) {assert(false);},12010000);
    assert(frame_model::live_buffers.load()==0);

    // Concurrent producer reset versus the UI callback; no rendering lock is
    // held by the callback, and the selected pixel backing remains referenced.
    std::atomic_bool done{false};
    std::thread producer([&] {
        for(int i=0;i<500;++i) {push(90000+i*1500);if(i%17==0) holder.cleanup();}
        done=true;
    });
    while(!done) holder.get([](AVFrame* f,uint64_t,bool) {
        std::this_thread::yield();
        assert(f->data[0][0]==static_cast<uint8_t>(f->pts));
    });
    producer.join();holder.cleanup();holder.releaseViewReference();
    assert(frame_model::live_buffers.load()==0);
}
