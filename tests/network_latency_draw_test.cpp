#include "mihoyo/network_latency_draw.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>
struct Text {float x,y;std::string value;NVGcolor color;};
static std::vector<Text> labels;
static NVGcolor fill;
static int saves=0;
NVGcolor nvgRGBA(unsigned char r,unsigned char g,unsigned char b,unsigned char a) {
    NVGcolor c{};c.r=r/255.f;c.g=g/255.f;c.b=b/255.f;c.a=a/255.f;return c;
}
void nvgSave(NVGcontext*) {++saves;}
void nvgRestore(NVGcontext*) {--saves;assert(saves>=0);}
void nvgIntersectScissor(NVGcontext*,float x,float y,float w,float h) {
    assert(std::isfinite(x+y+w+h) && w>=100 && h>=60);
}
void nvgFontSize(NVGcontext*,float size) {assert(size==18);}
void nvgTextAlign(NVGcontext*,int align) {assert(align==(NVG_ALIGN_RIGHT|NVG_ALIGN_BOTTOM));}
void nvgFillColor(NVGcontext*,NVGcolor color) {fill=color;}
float nvgText(NVGcontext*,float x,float y,const char* value,const char*) {
    labels.push_back({x,y,value,fill});return x;
}
// No path/fill/hit-target functions are defined: adding an opaque panel would
// fail this link. Both games use this same renderer, independent of HUD toggle.
int main() {
    using cloudgenshin::overlay::DrawNetworkLatency;
    DrawNetworkLatency(nullptr,40,30,1280,720,15);
    assert(labels.size()==2 && saves==0);
    assert(labels[0].value=="15 ms" && labels[1].value=="15 ms");
    assert(labels[1].x==1300 && labels[1].y==732);
    assert(labels[0].color.a<labels[1].color.a && labels[1].color.a<1);
    for(const auto rtt: {std::optional<int>{},std::optional<int>{-1},std::optional<int>{60001}}) {
        labels.clear();DrawNetworkLatency(nullptr,0,0,960,540,rtt);
        assert(labels.size()==2 && labels.back().value=="-- ms" && saves==0);
    }
    labels.clear();DrawNetworkLatency(nullptr,0,0,1280,720,0);
    assert(labels.back().value=="0 ms");
    labels.clear();DrawNetworkLatency(nullptr,0,0,80,40,15);
    assert(labels.empty() && saves==0);
    std::cout<<"PASS transparent lower-right RTT text, clipping and unknown state\n";
}
