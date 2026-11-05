#include "mihoyo/stream_health.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin::overlay;
    StreamHealth health;StreamHealthTotals t;
    assert(!health.sample(0,t,false));
    t.received=60;t.decoded=58;t.presented=50;t.bytes=500000;
    t.decode_count=58;t.decode_us=580000;t.queue_count=58;t.queue_us=116000;
    t.waiting_idr=2;t.display_skips=8;t.audio_played=48000;
    t.render_count=50;t.render_us=100000;t.poll_peak_us=278496;
    assert(health.sample(1000,t,false));
    auto w=health.window();assert(w.received_fps==60 && w.decoded_fps==58 && w.submitted_fps==50);
    assert(w.decode_ms==10 && w.queue_ms==2 && w.mbps==4 && w.audio_played_ms==1000);
    assert(w.waiting_idr==2 && w.display_skips==8 && !w.ui_active);
    assert(w.render_ms==2 && w.poll_peak_ms>278 && w.poll_peak_ms<279);
    StreamHealth detailed;StreamHealthTotals dt;assert(!detailed.sample(0,dt,false));
    dt.ui_draws=60;dt.ui_input_us=60000;dt.ui_video_us=120000;dt.ui_tail_us=180000;
    dt.decode_over16=4;dt.decode_over33=2;dt.decode_peak_us=75000;dt.ui_outside_peak_us=30000;
    assert(detailed.sample(1000,dt,false));const auto dw=detailed.window();
    assert(dw.ui_input_ms==1 && dw.ui_video_ms==2 && dw.ui_tail_ms==3 && dw.ui_outside_peak_ms==30);
    assert(dw.decode_over16==4 && dw.decode_over33==2 && dw.decode_peak_ms==75);
    assert(!health.sample(1250,t,true));assert(!health.sample(1500,t,false));
    t.display_skips+=4;assert(health.sample(2000,t,false));w=health.window();
    assert(w.ui_active && w.display_skips==4 && w.waiting_idr==0 && w.congestion==0);
    assert(health.sample(3000,{},false));w=health.window();
    assert(w.received_fps==0 && w.display_skips==0 && w.decode_ms==0);
    assert(!health.sample(1,{},false));
}
