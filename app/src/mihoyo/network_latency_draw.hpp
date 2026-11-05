#pragma once
#include <nanovg.h>
#include <optional>
#include <cstdio>

namespace cloudgenshin::overlay {
inline void DrawNetworkLatency(NVGcontext* vg,float x,float y,float width,float height,
    std::optional<int> rtt_ms) {
    if(width<100 || height<60) return;
    char label[24]{};
    if(rtt_ms && *rtt_ms>=0 && *rtt_ms<=60000)
        std::snprintf(label,sizeof(label),"%d ms",*rtt_ms);
    else std::snprintf(label,sizeof(label),"-- ms");
    nvgSave(vg);
    nvgIntersectScissor(vg,x,y,width,height);
    nvgFontSize(vg,18);nvgTextAlign(vg,NVG_ALIGN_RIGHT|NVG_ALIGN_BOTTOM);
    // No panel, hit target, focus item or touch handler. The offset shadow
    // keeps translucent text readable on both bright and dark game scenes.
    nvgFillColor(vg,nvgRGBA(0,0,0,155));
    nvgText(vg,x+width-19,y+height-17,label,nullptr);
    nvgFillColor(vg,nvgRGBA(240,248,250,225));
    nvgText(vg,x+width-20,y+height-18,label,nullptr);
    nvgRestore(vg);
}
} // namespace cloudgenshin::overlay
