#pragma once
#include "mihoyo/stream_overlay_policy.hpp"
#include <array>

namespace cloudgenshin::overlay {
// Original schematic, not a photograph or a downloaded controller asset.
// One geometry source drives painting AND hit testing. IDs are physical Switch
// fields (controller_mapping.hpp), not remote XInput labels.
class ControllerDiagramLayout {
public:
    explicit ControllerDiagramLayout(Rect bounds) {
        const Layout menu(bounds);
        const float width=menu.panel.x-bounds.x-32;
        if(width<520 || bounds.h<600) return; // Keep usable hit sizes; narrow views retain the list.
        panel={bounds.x+16,bounds.y+16,width,std::min(680.0f,bounds.h-32)};
        scale=std::min((panel.w-24)/640,(panel.h-84)/600);
        origin={panel.x+(panel.w-640*scale)/2,panel.y+64};
        visible=true;
    }
    Rect rect(float x,float y,float w,float h) const {
        return {origin.x+x*scale,origin.y+y*scale,w*scale,h*scale};
    }
    Rect button(unsigned source) const {
        if(!visible || source>=buttons.size()) return {};
        const auto r=buttons[source];return rect(r.x,r.y,r.w,r.h);
    }
    int hit(Point point) const {
        if(visible) for(unsigned i=0;i<buttons.size();++i) if(button(i).contains(point)) return static_cast<int>(i);
        return -1;
    }
    static constexpr int kTargetBase=100;
    bool visible=false;
    Rect panel;
    float scale=1;
    Point origin;
private:
    inline static constexpr std::array<Rect,16> buttons{{
        {562,158,36,36},{526,194,36,36},{526,122,36,36},{490,158,36,36}, // A/B/X/Y
        {52,52,100,32},{488,52,100,32},{56,414,112,40},{472,414,112,40}, // L/R/ZL/ZR
        {64,144,64,64},{512,254,64,64},{490,98,28,28},{120,98,28,28}, // L3/R3/+/-
        {80,222,36,36},{80,294,36,36},{44,258,36,36},{116,258,36,36} // D-pad
    }};
};
} // namespace cloudgenshin::overlay
