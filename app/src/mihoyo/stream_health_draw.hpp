#pragma once
#include "mihoyo/stream_health.hpp"
#include "mihoyo/ui_language.hpp"
#include <nanovg.h>
#include <cstdio>
namespace cloudgenshin::overlay {
inline void DrawStreamHealth(NVGcontext* vg,float x,float y,const std::string& resolution,
    bool hardware,const StreamHealthWindow& s) {
    // Transparent HUD: only text and a small offset shadow, no filled panel.
    // Detailed GPU/poll/decode-tail counters remain in the existing safe logs.
    nvgSave(vg);
    const auto line=[&](int row,const std::string& value,NVGcolor color=nvgRGB(225,238,244)) {
        nvgFontSize(vg,17);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);
        nvgFillColor(vg,nvgRGBA(0,0,0,215));
        nvgText(vg,x+15.5f,y+21.5f+row*27,value.c_str(),nullptr);
        nvgFillColor(vg,color);
        nvgText(vg,x+14,y+20+row*27,value.c_str(),nullptr);
    };
    char value[160]{};
    std::snprintf(value,sizeof(value),"   %.2f Mbps",s.mbps);
    line(0,resolution+"  "+ui::Text(hardware ? "Hardware decode" : "Software decode")+value);
    if(!s.ready) {line(1,ui::Text("Waiting for a diagnostic interval"));nvgRestore(vg);return;}
    std::snprintf(value,sizeof(value),": %.1f / %.1f / %.1f FPS",s.received_fps,s.decoded_fps,s.submitted_fps);
    line(1,ui::Text("Receive / decode / new submit")+value);
    std::snprintf(value,sizeof(value),": %.2f / %.2f / %.2f ms",s.decode_ms,s.queue_ms,s.render_ms);
    line(2,ui::Text("Decode / queue / render CPU")+value);
    std::snprintf(value,sizeof(value),": %llu / %llu",static_cast<unsigned long long>(s.waiting_idr),static_cast<unsigned long long>(s.congestion));
    line(3,ui::Text("Keyframe wait / decode queue drops")+value,
        s.waiting_idr || s.congestion ? nvgRGB(255,208,110) : nvgRGB(225,238,244));
    line(4,ui::Text(s.ui_active ? "Display skips during UI interval" : "Local display skips")+": "+std::to_string(s.display_skips));
    std::snprintf(value,sizeof(value),": %.0f ms | %llu / %llu",s.audio_played_ms,
        static_cast<unsigned long long>(s.audio_underruns),static_cast<unsigned long long>(s.audio_drops));
    line(5,ui::Text("Audio played / gaps / drops")+value);
    std::snprintf(value,sizeof(value),": %.1f FPS / %.1f ms",s.ui_fps,s.ui_peak_ms);
    line(6,ui::Text("UI FPS / recent longest gap")+value);
    line(7,ui::Text("Local timings, not network latency"),nvgRGB(190,210,220));
    nvgRestore(vg);
}
}
