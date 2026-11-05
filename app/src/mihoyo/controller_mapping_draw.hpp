#pragma once
#include "mihoyo/controller_mapping_editor.hpp"
#include "mihoyo/controller_mapping_diagram_draw.hpp"
#include "mihoyo/ui_language.hpp"
#include <nanovg.h>

namespace cloudgenshin::overlay {
// Caller selects the existing CN/EN font. No assets, keyboard dialog, heap
// widget tree or new activity is needed while the stream is running.
inline void DrawControllerMapping(NVGcontext* vg,Rect bounds,const input::ControllerMappingEditor& editor,
    const std::string& game_name,std::string_view layout,int motion_status=0) {
    const Layout box(bounds);const auto p=box.panel;
    const auto cyan=nvgRGB(90,225,235),white=nvgRGB(242,246,250),muted=nvgRGB(166,185,202);
    const auto fill=[&](Rect r,NVGcolor color) {
        nvgBeginPath(vg);nvgRoundedRect(vg,r.x,r.y,r.w,r.h,10);nvgFillColor(vg,color);nvgFill(vg);
    };
    const auto text=[&](float x,float y,const std::string& value,float size,NVGcolor color) {
        nvgFontSize(vg,size);nvgFillColor(vg,color);nvgTextAlign(vg,NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE);
        nvgText(vg,x,y,value.c_str(),nullptr);
    };
    nvgSave(vg);nvgIntersectScissor(vg,bounds.x,bounds.y,bounds.w,bounds.h);
    fill(bounds,nvgRGBA(0,0,0,100));
    DrawControllerDiagram(vg,bounds,editor,layout);
    fill(p,nvgRGBA(16,25,39,250));
    text(p.x+24,p.y+33,ui::Text("Controller mapping"),25,cyan);
    fill(box.close,nvgRGBA(255,255,255,16));text(box.close.x+17,box.close.y+24,"X",22,white);
    text(p.x+24,p.y+72,game_name+"  |  "+ui::Text("Saved separately per game"),17,white);
    std::string caption;
    if(editor.targets()) caption=ui::Text("Physical button")+" "+ui::Text(input::PhysicalButtonName(editor.source()))+"  >  "+ui::Text("Game button");
    else caption=ui::Text(editor.gyro_page()?(editor.gyro_supported()?"Gyro and control mode":"Gyro assistance"):
        "Physical button -> Game button");
    caption+="  ("+std::to_string(editor.page()+1)+"/"+(editor.targets()?"2":"3")+")";
    text(p.x+24,p.y+101,caption,19,cyan);
    text(p.x+24,p.y+128,ui::Text(editor.native_navigation()?"Native navigation bypasses saved combat mapping":
        "Local menus and exit shortcuts do not change"),15,muted);
    constexpr const char* states[]{"Native IMU: no activation recognized","Native IMU: server activated",
        "Local gyro: hold physical ZL","Local gyro: right stick composed",
        "Local gyro: sensor or input not ready","Local gyro: off","Local gyro: paused for navigation"};
    text(p.x+24,p.y+151,ui::Text(editor.gyro_supported()?states[std::clamp(motion_status,0,6)]:
        "Gyro assistance is currently Genshin only"),15,muted);
    nvgSave(vg);nvgIntersectScissor(vg,p.x+24,p.y+161,p.w-48,24);
    text(p.x+24,p.y+174,ui::Text(editor.feedback().empty()?"Changes apply only after SD save succeeds":editor.feedback()),15,cyan);
    nvgRestore(vg);
    for(int row=0;row<kMenuRows;++row) {
        const auto r=box.row(row);
        if(row==editor.focus()) fill({r.x,r.y+1,r.w,r.h-2},nvgRGBA(90,225,235,42));
        else if(row%2==0) fill({r.x,r.y+1,r.w,r.h-2},nvgRGBA(255,255,255,5));
        std::string label;
        bool enabled=true;
        if(editor.targets()) {
            if(row<9) {
                const unsigned choice=editor.page()*9+row;
                if(choice<=16) label=ui::Text(input::RemoteButtonName(choice?choice-1:input::kDisabledButton,layout));
            } else label=ui::Text(row==9?"Previous page":row==10?"Next page":"Back to mapping");
        } else if(row>=8) label=ui::Text(row==8?"Previous page":row==9?"Next page":row==10?"Restore game defaults":"Back to stream settings");
        else if(!editor.gyro_page()) {
            const unsigned source=editor.page()*8+row;
            enabled=source!=input::kFixedMinusButton;
            label=ui::Text(input::PhysicalButtonName(source))+"   >   "+ui::Text(enabled?
                input::RemoteButtonName(editor.current().target[source],layout):"Game Back / map (fixed)");
            if(enabled && editor.gyro_supported() && editor.current().target[source]<8)
                label+="  ("+ui::Text(input::GenshinDefaultActionName(editor.current().target[source]))+")";
        } else if(!editor.gyro_supported()) {
            if(row==0) label=ui::Text("Gyro assistance is currently Genshin only");
            enabled=false;
        } else if(row<5) {
            const auto& config=editor.current();
            if(row==0) label=ui::Text("Gyro -> right stick (experimental)")+": "+ui::Text(config.gyro_assist?"On":"Off");
            if(row==1) label=ui::Text("Gyro sensitivity")+": "+std::to_string(config.gyro_sensitivity)+"%";
            if(row==2) label=ui::Text(config.gyro_aim_only?"Activation: hold physical ZL":"Activation: always");
            if(row==3) label=ui::Text("Invert gyro Y")+": "+ui::Text(config.gyro_invert_y?"On":"Off");
            if(row==4) label=ui::Text("Invert gyro X")+": "+ui::Text(config.gyro_invert_x?"On":"Off");
        } else if(row==5) {
            label=ui::Text(editor.native_navigation()?"Controls: native menu (tap for combat)":
                "Controls: combat (tap for native menu)");
        } else if(row==6 && input::HasGenshinPositionPreset(editor.current())) {
            label=ui::Text("Preset: R attack / A dash / B jump");enabled=false;
        } else if(row==7 && input::HasGenshinPositionPreset(editor.current())) {
            label=ui::Text("Hold L: original wheel; gyro paused");enabled=false;
        }
        // Leave a bearing gutter for system/font-fallback glyphs at the left edge.
        nvgSave(vg);nvgIntersectScissor(vg,r.x+12,r.y,r.w-24,r.h);
        text(r.x+18,r.y+r.h/2,label,18,enabled?white:muted);nvgRestore(vg);
    }
    text(p.x+24,p.y+p.h-25,ui::Text("Touch or Up/Down + A. B goes back."),16,muted);
    nvgRestore(vg);
}
} // namespace cloudgenshin::overlay
