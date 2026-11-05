#include "mihoyo/controller_mapping_draw.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>
#include <fstream>
#include <filesystem>
#include <borealis/extern/nlohmann/json.hpp>
static std::vector<std::string> labels;
static int saves=0;
using Json=nlohmann::json;
struct DrawState {
    NVGcolor fill{},stroke{};
    float font_size=18,stroke_width=1;
    int align=NVG_ALIGN_LEFT|NVG_ALIGN_MIDDLE;
    Json clip=nullptr;
};
static DrawState state;
static std::vector<DrawState> states;
static Json path;
static std::vector<Json> commands;
static Json Color(NVGcolor color) {return {color.r,color.g,color.b,color.a};}
NVGcolor nvgRGBA(unsigned char r,unsigned char g,unsigned char b,unsigned char a) {
    NVGcolor c{};c.r=r/255.f;c.g=g/255.f;c.b=b/255.f;c.a=a/255.f;return c;
}
NVGcolor nvgRGB(unsigned char r,unsigned char g,unsigned char b) {return nvgRGBA(r,g,b,255);}
void nvgSave(NVGcontext*) {++saves;states.push_back(state);}
void nvgRestore(NVGcontext*) {assert(saves>0 && !states.empty());--saves;state=states.back();states.pop_back();}
void nvgBeginPath(NVGcontext*) {path=nullptr;}
void nvgRoundedRect(NVGcontext*,float x,float y,float w,float h,float radius) {
    assert(std::isfinite(x+y+w+h) && w>0 && h>0);
    path={{"kind","round_rect"},{"x",x},{"y",y},{"w",w},{"h",h},{"radius",radius}};
}
void nvgFillColor(NVGcontext*,NVGcolor color) {state.fill=color;}
void nvgFill(NVGcontext*) {
    auto command=path;command["color"]=Color(state.fill);command["clip"]=state.clip;commands.push_back(command);
}
void nvgStrokeColor(NVGcontext*,NVGcolor color) {state.stroke=color;}
void nvgStrokeWidth(NVGcontext*,float width) {state.stroke_width=width;}
void nvgStroke(NVGcontext*) {
    auto command=path;command["color"]=Color(state.stroke);command["clip"]=state.clip;
    command["stroke"]=state.stroke_width;commands.push_back(command);
}
void nvgFontSize(NVGcontext*,float size) {assert(size>0);state.font_size=size;}
void nvgTextAlign(NVGcontext*,int align) {state.align=align;}
void nvgIntersectScissor(NVGcontext*,float x,float y,float w,float h) {
    assert(w>0 && h>0);
    if(!state.clip.is_null()) {
        const auto clip=state.clip;
        const float right=std::min(x+w,clip[0].get<float>()+clip[2].get<float>());
        const float bottom=std::min(y+h,clip[1].get<float>()+clip[3].get<float>());
        x=std::max(x,clip[0].get<float>());y=std::max(y,clip[1].get<float>());
        w=std::max(0.f,right-x);h=std::max(0.f,bottom-y);
    }
    state.clip={x,y,w,h};
}
float nvgText(NVGcontext*,float x,float y,const char* text,const char*) {
    assert(std::isfinite(x+y));labels.emplace_back(text);
    commands.push_back({{"kind","text"},{"x",x},{"y",y},{"text",text},{"size",state.font_size},
        {"color",Color(state.fill)},{"align",state.align},{"clip",state.clip}});
    return 0;
}
static bool Has(std::string_view text) {for(const auto& label:labels) if(label.find(text)!=std::string::npos) return true;return false;}
int main(int argc,char** argv) {
    using namespace cloudgenshin;
    for(bool chinese:{true,false}) for(const auto profile:{"hk4e","hkrpg","endfield"}) {
        ui::chinese=chinese;
        input::ControllerMappingEditor editor;editor.open(profile,input::DefaultControllerMapping(profile));
        const auto draw=[&] {
            labels.clear();overlay::DrawControllerMapping(nullptr,{0,0,1280,720},editor,"Test Game","Xbox",0);
            assert(saves==0 && Has(ui::Text("Controller mapping")));
            assert(Has(ui::Text("Back to stream settings")) || Has(ui::Text("Back to mapping")));
        };
        draw();assert(Has("A   >   "));
        assert(Has(ui::Text("Visual button mapping")) && Has(ui::Text("Rear / shoulder view (schematic)")));
        editor.activate(0);draw();assert(Has(ui::Text("Disabled")));
        editor.activate(11);editor.activate(9);draw();assert(Has(ui::Text("Game Back / map (fixed)")));
        editor.activate(9);draw();
        if(std::string_view(profile)=="hk4e") {
            assert(Has(ui::Text("Gyro -> right stick (experimental)")));
            assert(Has(ui::Text("Controls: combat (tap for native menu)")));
            editor.activate(5);draw();
            assert(Has(ui::Text("Controls: native menu (tap for combat)")));
            assert(Has(ui::Text("Native navigation bypasses saved combat mapping")));
        }
        else assert(!Has(ui::Text("Gyro -> right stick (experimental)")));
    }
    assert(ui::Text("Controller mapping",true)=="手柄自定义映射");
    for(bool chinese:{true,false}) {
        ui::chinese=chinese;
        input::ControllerMappingEditor editor;editor.open("hk4e",input::DefaultControllerMapping("hk4e"));
        editor.activate(8);labels.clear();
        overlay::DrawControllerMapping(nullptr,{0,0,1280,720},editor,"Genshin","Xbox",5);
        assert(Has(ui::Text("Local gyro: off")) && saves==0);
        assert(Has(ui::Text("Gyro -> right stick (experimental)")));
        assert(!Has(ui::Text("PS5 identity test; local gyro-to-stick paused")));
        labels.clear();overlay::DrawControllerMapping(nullptr,{0,0,1280,720},editor,"Genshin","Xbox",6);
        assert(Has(ui::Text("Local gyro: paused for navigation")));
        editor.activate(9);editor.select_source(5);labels.clear();
        overlay::DrawControllerMapping(nullptr,{0,0,1280,720},editor,"Genshin","Xbox",0);
        assert(Has(ui::Text("Normal attack")));
    }
    assert(ui::ChineseLogText("1 t=1s MOTION_FEEDBACK_INVALID code=1\n").find("校验")!=std::string::npos);
    assert(ui::ChineseLogText("1 t=1s GYRO_STICK_ASSIST_FRAMES_SENT code=1\n").find("非云端回执")!=std::string::npos);
    if(argc==2) {
        const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
        for(bool chinese:{true,false}) for(bool target:{false,true}) {
            ui::chinese=chinese;
            input::ControllerMappingEditor editor;editor.open("hk4e",input::DefaultControllerMapping("hk4e"));
            editor.update({},{0,0,1280,720},false,0,{});
            if(target) editor.select_source(7);
            input::SwitchControllerState raw;raw.zl=true;editor.update(raw,{0,0,1280,720},false,0,{});
            commands.clear();labels.clear();
            overlay::DrawControllerMapping(nullptr,{0,0,1280,720},editor,ui::Text("Genshin Cloud"),"Xbox",0);
            const auto file=directory/(std::string(chinese?"zh":"en")+(target?"-target":"-front")+".json");
            std::ofstream out(file);out<<Json{{"width",1280},{"height",720},{"commands",commands}}.dump();
            out.flush();assert(out.good());
        }
    }
    ui::chinese=true;
    std::cout<<"PASS mapping draw command model: both languages, all games/pages, bounded geometry, balanced state\n";
}
