#include "ffmpeg_reference_model.hpp"
extern "C" {
#include <libavutil/frame.h>
}
#include <cstdlib>
#include <cstring>
#include <new>
namespace frame_model {
std::atomic<int> live_buffers{0},frame_allocations{0};
std::atomic<bool> fail_next_ref{false};
}
namespace {
struct Buffer {
    std::atomic<int> refs{1};
    void(*release)(void*,uint8_t*);
    void* opaque;
    uint8_t* data;
};
void release_malloc(void*,uint8_t* data) {std::free(data);}
}
extern "C" {
void* av_mallocz(size_t size) {return std::calloc(1,size);}
void av_free(void* pointer) {std::free(pointer);}
AVBufferRef* av_buffer_create(uint8_t* data,size_t size,void(*release)(void*,uint8_t*),void* opaque,int) {
    auto* backing=new Buffer;
    backing->release=release;backing->opaque=opaque;backing->data=data;
    auto* result=new AVBufferRef;
    result->buffer=reinterpret_cast<AVBuffer*>(backing);result->data=data;result->size=size;
    ++frame_model::live_buffers;return result;
}
AVBufferRef* av_buffer_ref(const AVBufferRef* source) {
    auto* result=new AVBufferRef(*source);
    ++reinterpret_cast<Buffer*>(source->buffer)->refs;return result;
}
void av_buffer_unref(AVBufferRef** reference) {
    if(!reference || !*reference) return;
    auto* backing=reinterpret_cast<Buffer*>((*reference)->buffer);
    delete *reference;*reference=nullptr;
    if(--backing->refs==0) {
        if(backing->release) backing->release(backing->opaque,backing->data);
        delete backing;--frame_model::live_buffers;
    }
}
AVFrame* av_frame_alloc(void) {
    auto* frame=new AVFrame{};frame->pts=AV_NOPTS_VALUE;frame->extended_data=frame->data;
    ++frame_model::frame_allocations;return frame;
}
void av_frame_unref(AVFrame* frame) {
    for(auto& ref:frame->buf) av_buffer_unref(&ref);
    *frame={};frame->pts=AV_NOPTS_VALUE;frame->extended_data=frame->data;
}
void av_frame_free(AVFrame** frame) {
    if(frame && *frame) {av_frame_unref(*frame);delete *frame;*frame=nullptr;}
}
int av_frame_ref(AVFrame* destination,const AVFrame* source) {
    if(frame_model::fail_next_ref.exchange(false)) return -12;
    *destination=*source;destination->extended_data=destination->data;
    for(unsigned i=0;i<AV_NUM_DATA_POINTERS;++i)
        if(source->buf[i]) destination->buf[i]=av_buffer_ref(source->buf[i]);
    return 0;
}
AVFrame* av_frame_clone(const AVFrame* source) {
    auto* result=av_frame_alloc();
    if(av_frame_ref(result,source)<0) av_frame_free(&result);
    return result;
}
int av_frame_get_buffer(AVFrame* frame,int) {
    const auto size=static_cast<size_t>(frame->width)*frame->height*3/2;
    auto* data=static_cast<uint8_t*>(std::malloc(size));
    if(!data) return -12;
    frame->buf[0]=av_buffer_create(data,size,release_malloc,nullptr,0);
    frame->data[0]=data;frame->linesize[0]=frame->width;
    frame->data[1]=data+frame->width*frame->height;
    const bool nv12=frame->format==AV_PIX_FMT_NV12;
    frame->linesize[1]=nv12 ? frame->width : frame->width/2;
    if(!nv12) {frame->data[2]=frame->data[1]+frame->width*frame->height/4;frame->linesize[2]=frame->width/2;}
    return 0;
}
}
