#pragma once
#include "home_background.hpp"
#include <borealis/extern/nanovg/nanovg.h>

namespace cloudgenshin::background {

// Original static vector artwork. No downloaded art, font, logo or character;
// no image allocation, file read or animation when the custom file is absent.
inline void DrawDefault(NVGcontext* vg,float x,float y,float width,float height,unsigned game) {
    const auto color=kPalettes[game<kPalettes.size()?game:0];
    nvgSave(vg);nvgIntersectScissor(vg,x,y,width,height);
    nvgBeginPath(vg);nvgRect(vg,x,y,width,height);
    nvgFillPaint(vg,nvgLinearGradient(vg,x,y,x+width,y+height,
        nvgRGB(9,18,31),nvgRGB(color.red/5,color.green/5,color.blue/5)));
    nvgFill(vg);
    const float cx=x+width*.83f,cy=y+height*.32f,radius=height*.56f;
    nvgBeginPath(vg);nvgCircle(vg,cx,cy,radius);
    nvgFillPaint(vg,nvgRadialGradient(vg,cx,cy,0,radius,
        nvgRGBA(color.red,color.green,color.blue,55),nvgRGBA(color.red,color.green,color.blue,0)));
    nvgFill(vg);
    for(unsigned ring=0;ring<3;++ring) {
        nvgBeginPath(vg);nvgCircle(vg,cx,cy,height*(.24f+.1f*ring));
        nvgStrokeColor(vg,nvgRGBA(color.red,color.green,color.blue,25));nvgStrokeWidth(vg,1);nvgStroke(vg);
    }
    nvgBeginPath(vg);nvgMoveTo(vg,x,y+height*.9f);
    nvgLineTo(vg,x+width*.68f,y+height*.52f);nvgLineTo(vg,x+width,y+height*.68f);
    nvgLineTo(vg,x+width,y+height);nvgLineTo(vg,x,y+height);nvgClosePath(vg);
    nvgFillColor(vg,nvgRGBA(4,11,21,105));nvgFill(vg);
    nvgRestore(vg);
}

} // namespace cloudgenshin::background
