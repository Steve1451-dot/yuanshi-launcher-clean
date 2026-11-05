#include "mihoyo/stream_overlay_policy.hpp"
#include <cassert>
#include <cmath>

int main() {
    using namespace cloudgenshin::overlay;
    const Rect bounds{0,0,1280,720};const Layout layout(bounds);
    assert(bounds.contains({layout.panel.x,layout.panel.y}));
    assert(bounds.contains({layout.panel.x+layout.panel.w-1,layout.panel.y+layout.panel.h-1}));
    assert(layout.row_height>=36 && layout.hit_row({0,0})==-1);
    for(int i=0;i<kMenuRows;++i) {
        const auto r=layout.row(i);
        assert(layout.hit_row({r.x+5,r.y+r.h/2})==i);
        assert(r.y+r.h<=layout.panel.y+layout.panel.h-45);
    }
    const Layout offset({100,200,1280,720});
    assert(offset.hit_row({layout.row(10).x+110,layout.row(10).y+210})==10);
    const Layout docked({0,0,1920,1080});assert(docked.panel.h==680 && docked.panel.w==540);
    FloatingBall ball;const auto centre=ball.centre(bounds);
    assert(!ball.update(true,3,centre,bounds) && ball.captured());
    assert(ball.update(false,0,{},bounds) && !ball.captured());
    assert(!ball.update(true,3,centre,bounds));
    ball.update(true,3,{centre.x-600,centre.y+70},bounds);
    assert(!ball.update(false,0,{},bounds)); // Drag must not also open the menu.
    assert(ball.centre(bounds).x==42);
    auto c=ball.centre(bounds);ball.update(true,1,c,bounds);
    ball.update(true,2,c,bounds);assert(!ball.update(false,0,{},bounds)); // Finger replacement.
    ball.update(true,1,{600,400},bounds);ball.update(true,1,c,bounds);
    assert(!ball.update(false,0,{},bounds)); // Starting elsewhere cannot become a tap.
    ball.update(true,1,c,bounds);ball.cancel();assert(!ball.update(false,0,{},bounds));
    c=ball.centre(bounds);ball.update(true,1,c,bounds);ball.update(true,1,{-500,9999},bounds);
    ball.update(false,0,{},bounds);assert(ball.centre(bounds).y==686);
    const auto resized=ball.centre({100,100,640,480});assert(resized.x>=134 && resized.y<=546);

    MenuTap tap;assert(!tap.update(true,1,4));assert(tap.update(false,0,-1)==4);
    tap.update(true,1,4);tap.update(true,1,5);assert(!tap.update(false,0,-1));
    tap.update(true,1,-1);tap.update(true,1,4);assert(!tap.update(false,0,-1));
    tap.update(true,1,4);tap.update(true,2,4);assert(!tap.update(false,0,-1));
    tap.cancel();tap.update(true,1,4);assert(!tap.update(false,0,-1));
    tap.update(true,1,-2);assert(tap.update(false,0,-1)==-2);

    SettingRequest setting;using S=SettingRequest::State;
    assert(!setting.pending() && setting.snapshot().state()==S::Unset);
    setting.request(1);const auto first=*setting.pending();assert(first.value()==1);
    setting.request(2);setting.complete(first,true);
    assert(setting.snapshot().state()==S::Queued && setting.snapshot().value()==2);
    const auto second=*setting.pending();setting.complete(second,false);
    assert(!setting.pending() && setting.snapshot().state()==S::Failed);
    setting.request(2);const auto retry=*setting.pending();setting.complete(second,true);
    assert(setting.snapshot().state()==S::Queued);setting.complete(retry,true);
    assert(setting.snapshot().state()==S::Sent && !setting.pending());
    SettingRequest fps;fps.request(60);fps.complete(*fps.pending(),true);
    assert(fps.snapshot().value()==60 && setting.snapshot().value()==2);

    StreamRates rates;assert(!rates.sample(0,100,1000000));
    assert(!rates.sample(999,130,2000000));assert(rates.sample(1000,130,2000000));
    assert(std::abs(rates.fps()-30)<0.001 && std::abs(rates.mbps()-8)<0.001);
    assert(rates.sample(3000,190,3000000));assert(rates.fps()==30 && rates.mbps()==4);
    assert(rates.sample(4000,190,3000000));assert(rates.fps()==0 && rates.mbps()==0);
    assert(rates.sample(5000,0,0));assert(rates.fps()==0 && rates.mbps()==0);
}
