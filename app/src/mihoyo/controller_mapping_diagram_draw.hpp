#pragma once
#include "mihoyo/controller_mapping_editor.hpp"
#include "mihoyo/ui_language.hpp"
#include <nanovg.h>

namespace cloudgenshin::overlay {
inline void DrawControllerDiagram(NVGcontext* vg,Rect bounds,const input::ControllerMappingEditor& editor,
                                  std::string_view remote_layout) {
    const ControllerDiagramLayout diagram(bounds);
    if(!diagram.visible) return;
    const auto p=diagram.panel;
    const auto cyan=nvgRGB(90,225,235),ink=nvgRGB(241,247,252),muted=nvgRGB(155,177,197);
    const auto round=[&](Rect r,NVGcolor fill,NVGcolor outline,float radius=12) {
        nvgBeginPath(vg);nvgRoundedRect(vg,r.x,r.y,r.w,r.h,radius*diagram.scale);
        nvgFillColor(vg,fill);nvgFill(vg);nvgStrokeWidth(vg,1.5f);nvgStrokeColor(vg,outline);nvgStroke(vg);
    };
    const auto text=[&](float x,float y,const std::string& value,float size,NVGcolor color) {
        const auto r=diagram.rect(x,y,1,1);
        nvgFontSize(vg,size*diagram.scale);nvgFillColor(vg,color);
        nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);nvgText(vg,r.x,r.y,value.c_str(),nullptr);
    };
    nvgSave(vg);nvgIntersectScissor(vg,p.x,p.y,p.w,p.h);
    round(p,nvgRGBA(16,25,39,250),nvgRGBA(126,169,196,40),16);
    nvgFontSize(vg,22);nvgFillColor(vg,cyan);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);
    nvgText(vg,p.x+24,p.y+30,ui::Text(editor.native_navigation()?"Native gamepad":"Visual button mapping").c_str(),nullptr);
    text(320,20,ui::Text(editor.native_navigation()?"Bindings are configured in the game":
        "Front view - tap a physical button"),17,muted);
    // Joy-Con sides and screen. No brand logo, no imaginary rear paddles.
    round(diagram.rect(44,90,558,260),nvgRGB(40,53,69),nvgRGB(72,94,116),30);
    round(diagram.rect(44,90,110,260),nvgRGB(29,71,81),nvgRGB(55,117,127),30);
    round(diagram.rect(486,90,116,260),nvgRGB(48,68,89),nvgRGB(78,112,142),30);
    round(diagram.rect(158,104,324,232),nvgRGB(10,19,30),nvgRGB(64,95,117),10);
    nvgSave(vg);
    const auto screen=diagram.rect(164,112,312,216);
    nvgIntersectScissor(vg,screen.x,screen.y,screen.w,screen.h);
    const auto selected=editor.highlighted_source();
    if(selected) {
        const auto source=*selected;
        text(320,160,ui::Text("Selected mapping"),17,muted);
        text(320,205,ui::Text(input::PhysicalButtonName(source))+"  >  "+
            ui::Text(source==input::kFixedMinusButton?"Game Back / map (fixed)":
                input::RemoteButtonName(editor.displayed().target[source],remote_layout)),source>=10?18:25,cyan);
        if(!editor.native_navigation() && editor.gyro_supported() && source!=input::kFixedMinusButton && editor.current().target[source]<8)
            text(320,235,ui::Text(input::GenshinDefaultActionName(editor.current().target[source])),17,ink);
        text(320,258,ui::Text(editor.native_navigation()?"Bindings are configured in the game":
            editor.targets()?"Choose the game button on the right":"Tap to change; saved per game"),15,muted);
    } else {
        text(320,186,ui::Text(editor.native_navigation()?"Native gamepad":"Tap a button to remap"),23,cyan);
        text(320,234,ui::Text("Local menus stay unchanged"),16,muted);
    }
    text(320,302,ui::Text("L3 / R3: press the sticks"),14,muted);
    nvgRestore(vg);
    // Rear/top schematic: ZL/ZR are triggers, not configurable extra paddles.
    text(320,388,ui::Text("Rear / shoulder view (schematic)"),17,muted);
    round(diagram.rect(56,460,528,112),nvgRGB(38,51,67),nvgRGB(68,91,113),35);
    round(diagram.rect(56,460,112,112),nvgRGB(29,71,81),nvgRGB(55,117,127),30);
    round(diagram.rect(472,460,112,112),nvgRGB(48,68,89),nvgRGB(78,112,142),30);
    text(320,511,ui::Text("ZL / ZR are shoulder triggers"),17,muted);
    constexpr const char* symbols[]{"A","B","X","Y","L","R","ZL","ZR","L3","R3","+","-","^","v","<",">"};
    for(unsigned source=0;source<input::kMappedButtons.size();++source) {
        const auto r=diagram.button(source);
        const bool focused=selected && *selected==source;
        const bool pressed=editor.pressed().*input::kMappedButtons[source];
        const bool reserved=source==input::kFixedMinusButton;
        const auto fill=pressed?nvgRGB(90,225,235):focused?nvgRGB(35,107,122):nvgRGB(18,33,48);
        const auto border=focused?cyan:pressed?ink:nvgRGB(101,133,157);
        round(r,fill,border,(source<4 || source==8 || source==9)?32:8);
        nvgFontSize(vg,(source==8 || source==9?16:17)*diagram.scale);
        nvgFillColor(vg,pressed?nvgRGB(10,26,38):reserved?muted:ink);
        nvgTextAlign(vg,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
        nvgText(vg,r.x+r.w/2,r.y+r.h/2,symbols[source],nullptr);
    }
    text(320,593,ui::Text("Highlighted = selected; bright = physically held"),14,muted);
    nvgRestore(vg);
}
} // namespace cloudgenshin::overlay
