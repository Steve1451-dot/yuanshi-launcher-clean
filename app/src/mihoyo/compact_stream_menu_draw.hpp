#pragma once
#include "mihoyo/compact_stream_menu.hpp"
#include "mihoyo/ui_language.hpp"
#include <nanovg.h>
#include <cstdio>
#include <string>

namespace cloudgenshin::overlay {
struct CompactStreamMenuStatus {
    std::string resolution,feedback;
    double decoded_fps=0,mbps=0;
    bool hardware=false,settings_ready=false,control_ready=false,continue_ready=false;
    bool hud=false,hardware_preferred=false,confirm_exit=false;
    bool smoothness_protection=false;
    int active_fps=0;
    SettingRequest::Snapshot quality{0},fps{0};
};
inline void DrawCompactStreamMenu(NVGcontext* vg,Rect bounds,const CompactStreamMenu& menu,
    int focus,const CompactStreamMenuStatus& status) {
    const CompactStreamMenuLayout layout(bounds,menu.count());const auto p=layout.panel;
    const auto white=nvgRGB(242,246,250),cyan=nvgRGB(90,225,235),muted=nvgRGB(176,192,207);
    const auto box=[&](Rect r,NVGcolor color,float radius=8) {
        nvgBeginPath(vg);nvgRoundedRect(vg,r.x,r.y,r.w,r.h,radius);nvgFillColor(vg,color);nvgFill(vg);
    };
    const auto text=[&](float x,float y,const std::string& value,float size,NVGcolor color,int align=NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE) {
        nvgFontSize(vg,size);nvgFillColor(vg,color);nvgTextAlign(vg,align);nvgText(vg,x,y,value.c_str(),nullptr);
    };
    const auto request_status=[](SettingRequest::State state) -> const char* {
        return state==SettingRequest::State::Queued ? "Pending send" : state==SettingRequest::State::Failed ?
            "Send failed / retry" : state==SettingRequest::State::Sent ? "Request sent" : "";
    };
    constexpr const char* quality_names[]{"High quality","Traffic saving","Extreme saving"};
    nvgSave(vg);nvgIntersectScissor(vg,p.x,p.y,p.w,p.h);
    // No full-screen dimmer. The game remains visible through the small panel.
    box(p,nvgRGBA(12,22,35,184),14);
    text(p.x+22,p.y+31,ui::Text(menu.title()),23,cyan);
    box(layout.close,nvgRGBA(255,255,255,20));
    text(layout.close.x+22,layout.close.y+22,"X",20,white,NVG_ALIGN_CENTER|NVG_ALIGN_MIDDLE);
    char summary[128]{};
    std::snprintf(summary,sizeof(summary),"   %.1f FPS   %.2f Mbps",status.decoded_fps,status.mbps);
    text(p.x+22,p.y+67,status.resolution+summary,16,white);
    text(p.x+22,p.y+91,ui::Text(status.smoothness_protection ? "Smoothness protection" :
        status.hardware?"Hardware decode":"Software decode")+"  |  "+
        ui::Text(status.control_ready?"Control ready":"Control not ready"),14,muted);
    for(int i=0;i<menu.count();++i) {
        const auto r=layout.row(i);const int command=menu.command(i);
        if(i==focus) box({r.x,r.y+1,r.w,r.h-2},nvgRGBA(90,225,235,42));
        const bool enabled=command>=0 && command<=4 ? status.settings_ready :
            command==9 ? status.continue_ready : true;
        std::string tag;
        if(command==-1) tag=status.quality.state()==SettingRequest::State::Unset ? "--" :
            ui::Text(quality_names[std::clamp(status.quality.value(),0,2)]);
        if(command==-2) tag=status.fps.state()==SettingRequest::State::Unset ? "--" :
            std::to_string(status.fps.value()==30?30:60)+" FPS";
        const bool auto_fps=status.active_fps==30 && status.fps.value()==60 && status.smoothness_protection &&
            status.fps.state()==SettingRequest::State::Sent;
        if(command==-2 && auto_fps) tag=ui::Text("Auto: 30 FPS");
        if(command>=0 && command<=2 && status.quality.value()==command)
            tag=ui::Text(request_status(status.quality.state()));
        if((command==3 || command==4) && status.fps.value()==(command==3?30:60))
            tag=ui::Text(auto_fps ? "Auto: 30 FPS" : request_status(status.fps.state()));
        if(command==5) tag=ui::Text(status.hud?"On":"Off");
        if(command==11) tag=ui::Text(status.hardware_preferred?"HW preferred":"Software decode");
        const auto label=ui::Text(command==10 && status.confirm_exit?"Confirm stop + return":menu.label(i));
        const bool failed=(command>=0 && command<=2 && status.quality.state()==SettingRequest::State::Failed &&
            status.quality.value()==command) || ((command==3 || command==4) &&
            status.fps.state()==SettingRequest::State::Failed && status.fps.value()==(command==3?30:60));
        nvgSave(vg);nvgIntersectScissor(vg,r.x+12,r.y,r.w-(tag.empty()?24:164),r.h);
        text(r.x+12,r.y+r.h/2,label,18,command==10?nvgRGB(255,154,136):enabled?white:muted);
        nvgRestore(vg);
        if(!tag.empty()) text(r.x+r.w-12,r.y+r.h/2,tag,14,failed?nvgRGB(255,154,136):cyan,NVG_ALIGN_RIGHT|NVG_ALIGN_MIDDLE);
    }
    nvgSave(vg);nvgIntersectScissor(vg,p.x+22,p.y+p.h-36,p.w-44,28);
    text(p.x+22,p.y+p.h-22,ui::Text(status.feedback.empty()?"Touch or Up/Down + A. B goes back.":status.feedback),14,cyan);
    nvgRestore(vg);nvgRestore(vg);
}
} // namespace cloudgenshin::overlay
