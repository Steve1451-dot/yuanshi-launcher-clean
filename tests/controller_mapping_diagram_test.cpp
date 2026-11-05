#include "mihoyo/controller_mapping_diagram.hpp"
#include <cassert>
#include <iostream>
using namespace cloudgenshin::overlay;
int main() {
    for(const auto bounds:{Rect{0,0,1280,720},Rect{0,0,1920,1080},Rect{32,48,1280,720}}) {
        ControllerDiagramLayout diagram(bounds);const Layout menu(bounds);
        assert(diagram.visible && diagram.panel.x+diagram.panel.w<menu.panel.x);
        for(unsigned source=0;source<16;++source) {
            const auto r=diagram.button(source);
            assert(r.w>=24 && r.h>=24 && diagram.panel.contains({r.x,r.y}));
            assert(diagram.panel.contains({r.x+r.w-0.01f,r.y+r.h-0.01f}));
            assert(diagram.hit({r.x+r.w/2,r.y+r.h/2})==static_cast<int>(source));
            for(unsigned other=0;other<16;++other) if(other!=source) {
                const auto b=diagram.button(other);
                constexpr float rounding=0.001f; // Independently scaled shared edges may differ by one ULP.
                assert(r.x+r.w<=b.x+rounding || b.x+b.w<=r.x+rounding ||
                    r.y+r.h<=b.y+rounding || b.y+b.h<=r.y+rounding);
            }
        }
        assert(diagram.hit({bounds.x-1,bounds.y-1})==-1);
        assert(diagram.hit({diagram.panel.x+5,diagram.panel.y+5})==-1);
        assert(diagram.button(16).w==0);
    }
    for(const auto bounds:{Rect{0,0,960,720},Rect{0,0,640,480},Rect{0,0,1280,540}}) {
        ControllerDiagramLayout small(bounds);assert(!small.visible && small.hit({100,100})==-1);
    }
    std::cout<<"PASS diagram: all 16 physical hit regions, no overlap, viewport offsets, minimum size, narrow fallback\n";
}
