#include "mihoyo/presentation_diagnostics.hpp"
#include <cassert>
#include <map>
#include <string>
#include <thread>
int main() {
    using namespace cloudgenshin::overlay;
    PresentationDiagnostics debug;StreamHealth health;StreamHealthTotals t;VideoRenderStats stats{};
    debug.observe(0,&stats);debug.append(t,366400,0);assert(!health.sample(0,t,false));
    for(int i=1;i<=60;++i) {
        stats.command_busy=i/2;stats.upload_busy=i/3;
        debug.observe(i*16667,&stats);debug.append(t,1000,static_cast<unsigned>(i));
        const bool sampled=health.sample(i*16667/1000,t,false);
        if(sampled) debug.publish(health.window());
    }
    auto s=health.window();assert(s.ui_fps>59 && s.ui_fps<61 && s.hw_frames==60);
    assert(s.command_busy==30 && s.upload_busy==20 && s.ui_peak_ms<17);
    assert(s.poll_recent_ms==366.4);
    debug.observe(2000000,&stats);debug.append(t,2000,61);assert(health.sample(2000,t,false));
    s=health.window();assert(s.poll_recent_ms==2 && s.ui_peak_ms>900 && s.ui_slow==1);
    debug.publish(s);std::map<std::string,int> record;
    debug.report([&](const char* name,int value,int http) {assert(http==0);record[name]=value;});
    assert(record["DEBUG_WINDOW_SAMPLE_BEGIN"]==record["DEBUG_WINDOW_SAMPLE_END"]);
    assert(record["DEBUG_WINDOW_UI_OVER_50MS"]==1 && record["DEBUG_WINDOW_HW_FRAMES"]==1);
    PresentationDiagnostics phases;StreamHealthTotals pt;
    phases.beginDraw(0);phases.videoBegin(1000);phases.observe(3000,&stats);phases.endDraw(6000);
    phases.beginDraw(16000);phases.videoBegin(17000);phases.observe(19000,&stats);phases.endDraw(22000);
    phases.append(pt,0,0);
    assert(pt.ui_draws==2 && pt.ui_input_us==2000 && pt.ui_video_us==4000 && pt.ui_tail_us==6000);
    assert(pt.ui_outside_peak_us==10000);
    std::atomic_bool done{false};
    std::thread writer([&] {
        for(int i=1;i<5000;++i) {StreamHealthWindow next;next.sample=i;next.ui_fps=i;next.decoded_fps=i;debug.publish(next);}
        done=true;
    });
    while(!done) {
        record.clear();
        debug.report([&](const char* name,int value,int) {record[name]=value;});
        if(!record.contains("DEBUG_WINDOW_SNAPSHOT_BUSY")) {
            const auto sample=record["DEBUG_WINDOW_SAMPLE"];
            // The first observation may still be the preceding serial test.
            if(record["DEBUG_WINDOW_DURATION_MS"]==0 && sample>0)
                assert(record["DEBUG_WINDOW_UI_MILLIFPS"]==record["DEBUG_WINDOW_DECODE_MILLIFPS"]);
        }
    }
    writer.join();
}
