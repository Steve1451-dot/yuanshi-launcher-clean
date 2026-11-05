#include "mihoyo/compact_stream_menu_draw.hpp"
#include "mihoyo/stream_health_draw.hpp"
#include <cassert>
#include <cmath>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <borealis/extern/nlohmann/json.hpp>

struct TextCall {std::string value;float x,y;NVGcolor color;};
struct FillCall {float x,y,w,h;NVGcolor color;};
static std::vector<TextCall> texts;
static std::vector<FillCall> fills;
static NVGcolor color;
static FillCall path{};
static int depth=0;
using Json=nlohmann::json;
struct DrawState {NVGcolor color{};float size=18;int align=0;Json clip=nullptr;};
static DrawState draw_state;
static std::vector<DrawState> states;
static std::vector<Json> commands;
static float radius=0;
static Json Color(NVGcolor value) {return {value.r,value.g,value.b,value.a};}
NVGcolor nvgRGBA(unsigned char r,unsigned char g,unsigned char b,unsigned char a) {
    NVGcolor value{};value.r=r/255.f;value.g=g/255.f;value.b=b/255.f;value.a=a/255.f;return value;
}
NVGcolor nvgRGB(unsigned char r,unsigned char g,unsigned char b) {return nvgRGBA(r,g,b,255);}
void nvgSave(NVGcontext*) {++depth;draw_state.color=color;states.push_back(draw_state);}
void nvgRestore(NVGcontext*) {
    assert(depth>0);--depth;draw_state=states.back();states.pop_back();color=draw_state.color;
}
void nvgBeginPath(NVGcontext*) {path={};}
void nvgRoundedRect(NVGcontext*,float x,float y,float w,float h,float r) {
    assert(w>0 && h>0);path={x,y,w,h,{}};radius=r;
}
void nvgFillColor(NVGcontext*,NVGcolor value) {color=value;}
void nvgFill(NVGcontext*) {
    path.color=color;fills.push_back(path);
    commands.push_back({{"kind","round_rect"},{"x",path.x},{"y",path.y},{"w",path.w},{"h",path.h},
        {"radius",radius},{"color",Color(color)},{"clip",draw_state.clip}});
}
void nvgFontSize(NVGcontext*,float size) {assert(size>0);draw_state.size=size;}
void nvgTextAlign(NVGcontext*,int align) {draw_state.align=align;}
void nvgIntersectScissor(NVGcontext*,float x,float y,float w,float h) {
    assert(w>0 && h>0);
    if(!draw_state.clip.is_null()) {
        const auto& clip=draw_state.clip;
        const float right=std::min(x+w,clip[0].get<float>()+clip[2].get<float>());
        const float bottom=std::min(y+h,clip[1].get<float>()+clip[3].get<float>());
        x=std::max(x,clip[0].get<float>());y=std::max(y,clip[1].get<float>());
        w=std::max(0.f,right-x);h=std::max(0.f,bottom-y);
    }
    draw_state.clip={x,y,w,h};
}
float nvgText(NVGcontext*,float x,float y,const char* text,const char*) {
    assert(std::isfinite(x+y));texts.push_back({text,x,y,color});
    commands.push_back({{"kind","text"},{"x",x},{"y",y},{"text",text},{"size",draw_state.size},
        {"color",Color(color)},{"align",draw_state.align},{"clip",draw_state.clip}});
    return 0;
}
static bool Has(const std::string& value) {
    for(const auto& text:texts) if(text.value.find(value)!=std::string::npos) return true;
    return false;
}
int main(int argc,char** argv) {
    using namespace cloudgenshin;
    using namespace cloudgenshin::overlay;
    CompactStreamMenuStatus status;
    status.resolution="1280 x 720";status.decoded_fps=60;status.mbps=2.75;
    status.control_ready=true;status.settings_ready=true;status.hud=true;status.hardware=true;
    SettingRequest quality,fps;quality.request(2);fps.request(60);
    status.quality=quality.snapshot();status.fps=fps.snapshot();
    for(bool chinese:{false,true}) {
        ui::chinese=chinese;
        quality.request(2);status.quality=quality.snapshot();
        for(int page:{-1,0,1,4}) {
            CompactStreamMenu menu;if(page>=0) menu.select(page,2,60);
            texts.clear();fills.clear();
            DrawCompactStreamMenu(nullptr,{0,0,1280,720},menu,0,status);
            assert(depth==0 && Has(ui::Text(menu.title())));
            for(int row=0;row<menu.count();++row) assert(Has(ui::Text(menu.label(row))));
            assert(Has("60.0 FPS") && Has("2.75 Mbps"));
            assert(Has(ui::Text("Hardware decode")) && Has(ui::Text("Control ready")));
            for(const auto& fill:fills) {
                assert(fill.w<=440 && fill.h<=428); // No full-screen dimmer.
                assert(fill.color.a<=184/255.f); // All panel fills translucent.
            }
            if(page==-1) {
                assert(Has(ui::Text("Extreme saving")) && Has("60 FPS"));
                assert(!Has(ui::Text("Stream diagnostics")));
                assert(!Has(ui::Text("Decoder (next session)")));
            }
        }
        CompactStreamMenu menu;status.confirm_exit=true;texts.clear();
        DrawCompactStreamMenu(nullptr,{0,0,1280,720},menu,5,status);
        assert(Has(ui::Text("Confirm stop + return")));
        status.confirm_exit=false;
        quality.complete(*quality.pending(),false);status.quality=quality.snapshot();
        menu.select(0,2,60);texts.clear();
        DrawCompactStreamMenu(nullptr,{0,0,1280,720},menu,2,status);
        assert(Has(ui::Text("Send failed / retry")));

        StreamHealthWindow sample;sample.mbps=2.75;
        for(bool ready:{false,true}) {
            sample.ready=ready;sample.received_fps=60;sample.decoded_fps=59;sample.submitted_fps=58;
            sample.decode_ms=4.2;sample.waiting_idr=2;sample.audio_underruns=1;
            texts.clear();fills.clear();
            DrawStreamHealth(nullptr,20,18,"1280 x 720",true,sample);
            assert(depth==0 && fills.empty()); // HUD never draws a filled panel.
            assert(texts.size()==(ready?16u:4u)); // 8/2 lines, shadow + foreground.
            for(std::size_t i=0;i<texts.size();i+=2) {
                assert(texts[i].value==texts[i+1].value);
                assert(texts[i].color.r==0 && texts[i].color.g==0 && texts[i].color.b==0);
                assert(texts[i].x>texts[i+1].x && texts[i].y>texts[i+1].y);
            }
            if(ready) assert(Has(ui::Text("Local timings, not network latency")) && Has("60.0 / 59.0 / 58.0 FPS"));
            else assert(Has(ui::Text("Waiting for a diagnostic interval")));
        }
    }
    assert(ui::Text("More settings",true)=="更多设置");
    assert(ui::Text("Quality",true)=="画质");
    assert(ui::Text("Game Back / map (fixed)",true).find("地图")!=std::string::npos);
    assert(ui::Text("Touch ball: settings / Hold L3+R3: diagnostics / Minus: game map",true).find("双摇杆")!=std::string::npos);
    assert(ui::Text("Local timings, not network latency",true).find("非网络延迟")!=std::string::npos);
    if(argc==2) {
        const std::filesystem::path directory(argv[1]);std::filesystem::create_directories(directory);
        for(bool chinese:{true,false}) for(int page:{-1,0,1,4}) {
            ui::chinese=chinese;CompactStreamMenu menu;if(page>=0) menu.select(page,2,60);
            commands.clear();
            StreamHealthWindow sample;sample.ready=true;sample.mbps=2.75;
            sample.received_fps=60;sample.decoded_fps=59.8;sample.submitted_fps=59.5;
            sample.decode_ms=4.2;sample.queue_ms=1.1;sample.render_ms=.34;sample.ui_fps=60;sample.ui_peak_ms=18;
            DrawStreamHealth(nullptr,20,18,"1280 x 720",true,sample);
            DrawCompactStreamMenu(nullptr,{0,0,1280,720},menu,0,status);
            const Json fixture{{"width",1280},{"height",720},{"commands",commands},{"checker_background",true}};
            std::ofstream output(directory/(std::string(chinese?"zh":"en")+"-page-"+std::to_string(page)+".json"));
            output<<fixture.dump();assert(output.good());
        }
    }
}
