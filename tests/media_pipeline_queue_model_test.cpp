#include "mihoyo/media_pipeline.hpp"
#include "mihoyo/media_diagnostics.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <thread>
#include <vector>
#include <map>
#include <string>
// Production MediaPipeline/recovery/pool/thread lifecycle; decoder/audio are
// deterministic stand-ins. This is NOT an FFmpeg/NVDEC/audio integration test.
namespace {
std::atomic_bool blocked{false},entered{false};
std::atomic_int fail_hardware{0};
std::atomic_uint resets{0};
std::atomic_uint64_t decoded_pts{0};
std::mutex gate_mutex;std::condition_variable gate;
// A lost stop wake must fail CI rather than hang the surrounding test runner.
class TestDeadline {
public:
    TestDeadline():worker_([this] {
        std::unique_lock lock(mutex_);
        if(!wake_.wait_for(lock,std::chrono::seconds(30),[this]{return finished_;})) std::abort();
    }) {}
    ~TestDeadline() {
        {std::lock_guard lock(mutex_);finished_=true;}
        wake_.notify_all();worker_.join();
    }
private:
    std::mutex mutex_;std::condition_variable wake_;bool finished_=false;std::thread worker_;
};
template<class Predicate> void await(Predicate predicate) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    while(!predicate() && std::chrono::steady_clock::now()<end) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    assert(predicate());
}
void send(cloudgenshin::MediaPipeline& pipeline,unsigned pts,bool idr=true) {
    std::vector<uint8_t> bytes(16000,static_cast<uint8_t>(pts));
    bytes[0]=bytes[1]=bytes[2]=0;bytes[3]=1;bytes[4]=idr ? 0x65 : 0x41;
    PeerVideoPacket packet{bytes.data(),bytes.size(),pts,0};pipeline.video(packet);
    // The producer's RTP memory may be reused as soon as the callback returns.
    std::fill(bytes.begin(),bytes.end(),0);
}
}
struct AudioPipeline::Impl {};
AudioPipeline::AudioPipeline()=default;AudioPipeline::~AudioPipeline()=default;
bool AudioPipeline::start() {return true;}
void AudioPipeline::configure(int,int) {} void AudioPipeline::stop() {}
FFmpegVideoDecoder::FFmpegVideoDecoder()=default;
FFmpegVideoDecoder::~FFmpegVideoDecoder()=default;
int FFmpegVideoDecoder::setup(int,int,int,int,void*,int flags) {m_uses_hardware_frames=(flags&VIDEO_DECODER_PREFER_HARDWARE)!=0;m_frames_out=0;return 0;}
void FFmpegVideoDecoder::cleanup() {}
void FFmpegVideoDecoder::reset_stream() {++resets;}
int FFmpegVideoDecoder::capabilities() const {return 0;}
VideoDecodeStats* FFmpegVideoDecoder::video_decode_stats() {return &m_video_decode_stats_cache;}
int FFmpegVideoDecoder::submit_decode_unit(uint8_t* bytes,int size,int64_t pts) {
    entered=true;
    {std::unique_lock lock(gate_mutex);gate.wait(lock,[]{return !blocked.load();});}
    assert(size==16000 && bytes[5]==static_cast<uint8_t>(pts) && bytes[size-1]==static_cast<uint8_t>(pts));
    if(m_uses_hardware_frames && fail_hardware.exchange(0)) return -17;
    if(pts>=0 && pts<64) decoded_pts.fetch_or(std::uint64_t{1}<<pts);
    ++m_frames_out;return 1;
}
int main() {
    TestDeadline deadline;
    cloudgenshin::MediaPipeline pipeline;assert(pipeline.start(60));
    for(unsigned i=0;i<200;++i) {
        send(pipeline,i);
        await([&]{return pipeline.decoded()==i+1 && pipeline.performance().buffer_reserved>0;});
    }
    auto p=pipeline.performance();assert(p.buffer_allocations==1 && p.buffer_reuses==199);
    assert(p.buffer_reserved<=opennow::video::DecodeBufferPool::kMaximumBytes);
    std::uint64_t healthy_generation=0,recovery_generation=0;
    assert(!pipeline.take_keyframe_request(&healthy_generation));
    // RTP damage must gate dependent pictures immediately, while keeping the
    // generation and every already-complete queued unit valid.
    const auto damage_before=pipeline.performance().video_damage_events;
    const auto waiting_before=pipeline.performance().waiting_idr_drops;
    pipeline.notify_video_damage();
    assert(pipeline.performance().video_damage_events==damage_before+1);
    assert(pipeline.take_keyframe_request());
    send(pipeline,200,false);
    assert(pipeline.performance().waiting_idr_drops==waiting_before+1);
    assert(pipeline.decoded()==200);
    send(pipeline,201,true);await([&]{return pipeline.decoded()==201 && pipeline.performance().recovery.completed==1;});
    assert(!pipeline.take_keyframe_request(&healthy_generation));
    assert(!pipeline.take_keyframe_request());
    pipeline.request_resync();
    assert(pipeline.take_keyframe_request(&recovery_generation));
    assert(recovery_generation!=healthy_generation);
    send(pipeline,202);await([&]{return pipeline.decoded()==202 && pipeline.performance().recovery.completed==2;});
    assert(!pipeline.take_keyframe_request(&healthy_generation));
    assert(healthy_generation==recovery_generation);
    pipeline.stop();assert(pipeline.performance().buffer_reserved==0);
    // Simulate the Endfield owner closing a damaged AU during a receive burst:
    // two complete pictures are already queued while one decode is in flight.
    // They, and a later good IDR, must all survive repeated damage notices.
    assert(pipeline.start(60));blocked=true;entered=false;decoded_pts=0;
    send(pipeline,1);await([]{return entered.load();});
    const auto reset_before=resets.load();
    assert(!pipeline.take_keyframe_request(&healthy_generation));
    send(pipeline,2,false);send(pipeline,3,false);
    assert(pipeline.queue_depth()==2);
    pipeline.notify_video_damage();pipeline.notify_video_damage();
    assert(pipeline.queue_depth()==2 && pipeline.take_keyframe_request(&recovery_generation));
    assert(recovery_generation==healthy_generation && pipeline.performance().resync_events==0);
    assert(pipeline.performance().recovery.episodes==1 && pipeline.performance().recovery.phase==1);
    send(pipeline,4,false);assert(pipeline.queue_depth()==2);
    send(pipeline,5,true);send(pipeline,6,false);
    assert(pipeline.queue_depth()==4 && !pipeline.take_keyframe_request());
    assert(pipeline.performance().recovery.phase==2 && pipeline.performance().recovery.completed==0);
    blocked=false;gate.notify_all();await([&]{return pipeline.decoded()==5 && pipeline.performance().recovery.completed==1;});
    assert(decoded_pts==0x6e && resets==reset_before); // 1,2,3,5,6; not lost-dependent 4.
    assert(pipeline.performance().video_damage_events==2 && pipeline.performance().resync_drops==0);
    std::map<std::string,int> report;
    cloudgenshin::ReportMediaPerformance(pipeline.performance(),[&](const char* key,int value,int){report[key]=value;});
    assert(report.at("VIDEO_RECOVERY_EPISODES")==1 && report.at("VIDEO_RECOVERY_COMPLETED")==1);
    assert(report.at("VIDEO_RECOVERY_PHASE")==0 && report.at("VIDEO_RECOVERY_SUPERSEDED")==0);
    assert(report.at("VIDEO_RECOVERY_TOTAL_LAST_US")>=report.at("VIDEO_RECOVERY_IDR_LAST_US"));
    assert(report.at("VIDEO_RECOVERY_OUTPUT_MAX_US")==report.at("VIDEO_RECOVERY_OUTPUT_LAST_US"));
    pipeline.stop();
    // A brief repaired burst at 30 FPS must not flush valid references just
    // because five pictures arrived together. Decoding remains age bounded.
    assert(pipeline.start(30));blocked=true;entered=false;
    send(pipeline,1);await([]{return entered.load();});
    for(unsigned i=2;i<=6;++i) send(pipeline,i,false);
    assert(pipeline.queue_depth()==5 && pipeline.performance().congestion_events==0);
    blocked=false;gate.notify_all();await([&]{return pipeline.decoded()==6;});
    assert(!pipeline.take_keyframe_request());pipeline.stop();
    // With no further network arrivals, a stalled decode must still discard
    // stale queued units and request a fresh IDR rather than replay old work.
    assert(pipeline.start(30));blocked=true;entered=false;
    send(pipeline,1);await([]{return entered.load();});send(pipeline,2,false);send(pipeline,3,false);
    std::this_thread::sleep_for(std::chrono::milliseconds(90));
    blocked=false;gate.notify_all();
    await([&]{return pipeline.performance().congestion_events==1 && pipeline.queue_depth()==0;});
    assert(pipeline.decoded()==1 && pipeline.take_keyframe_request());
    send(pipeline,4);await([&]{return pipeline.decoded()==2;});pipeline.stop();
    assert(pipeline.start(60));blocked=true;entered=false;send(pipeline,1);await([]{return entered.load();});
    for(unsigned i=2;i<80;++i) send(pipeline,i,false);
    assert(pipeline.queue_depth()<=8 && pipeline.performance().congestion_events>0);
    assert(pipeline.take_keyframe_request());blocked=false;gate.notify_all();
    await([&]{return pipeline.decoded()>0;});send(pipeline,100,true);
    await([&]{return pipeline.decoded()>=2;});pipeline.stop();
    fail_hardware=1;assert(pipeline.start(60,40,true));send(pipeline,1);
    await([&]{return pipeline.performance().hardware_fallbacks==1 && !pipeline.hardware_decoder();});
    send(pipeline,2);await([&]{return pipeline.decoded()==1;});
    assert(pipeline.performance().hardware_runtime_error==-17);pipeline.stop();
    for(int i=0;i<20;++i) {assert(pipeline.start(30));send(pipeline,1);await([&]{return pipeline.decoded()==1;});pipeline.stop();}
    // Hit the wait entry/stop race both before the worker receives anything
    // and just as it returns from a decode to an empty queue.
    for(int i=0;i<1000;++i) {
        assert(pipeline.start(30));
        if(i%3==0) {send(pipeline,1);await([&]{return pipeline.decoded()==1;});}
        pipeline.request_stop();pipeline.request_stop();pipeline.stop();
    }
}
