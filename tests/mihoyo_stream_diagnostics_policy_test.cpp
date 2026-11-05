#include "mihoyo/stream_diagnostics_policy.hpp"
#include <cassert>

int main() {
    using namespace cloudgenshin;
    assert(EffectiveStreamFps(0)==30);
    assert(EffectiveStreamFps(-1)==30);
    assert(EffectiveStreamFps(1)==1);
    assert(EffectiveStreamFps(60)==60);
    assert(EffectiveStreamFps(120)==60);
    ExitHold hold;
    assert(!hold.update(true,false,0));
    assert(!hold.update(false,true,1000));
    assert(!hold.update(true,true,2000));
    assert(!hold.update(true,true,2799));
    assert(hold.update(true,true,2800));
    assert(!hold.update(false,false,3000));
    assert(!hold.update(true,true,5000));
    assert(!hold.update(true,false,5700));
    assert(!hold.update(true,true,5800));
    assert(!hold.update(true,true,6599));
    assert(hold.update(true,true,6600));
    ExitHold zero;
    assert(!zero.update(true,true,0));
    assert(zero.update(true,true,800));
    // A latched exit must not re-fire every frame while the combo is still
    // held: the stream view polls the pad state on each draw, so a held combo
    // used to request the activity pop on every frame.
    assert(!zero.update(true,true,900));
    assert(!zero.update(true,true,5000));
    assert(!zero.update(false,true,5100));
    assert(!zero.update(true,true,5200));
    assert(!zero.update(true,true,5999));
    assert(zero.update(true,true,6000));
}
