#include "mihoyo/controller_source_policy.hpp"
#include <cassert>

int main() {
    using namespace cloudgenshin::input;
    ControllerSource source;
    std::array<bool,8> players{};
    assert(source.select(false,players).source==-1);
    players[0]=true;players[3]=true;
    auto selected=source.select(true,players);assert(selected.source==0 && selected.changed);
    assert(!source.select(true,players).changed); // Attached Joy-Con wins, external pad ignored.
    selected=source.select(false,players);assert(selected.source==1 && selected.changed);
    players[0]=false;
    selected=source.select(false,players);assert(selected.source==4 && selected.changed);
    players[0]=true;
    assert(source.select(false,players).source==4); // A new idle pad must not steal input.
    selected=source.select(true,players);assert(selected.source==0 && selected.changed);
    players={};
    selected=source.select(false,players);assert(selected.source==-1 && selected.changed);
    assert(!source.select(false,players).changed);
    ControllerNeutralHandoff handoff;
    assert(!handoff.pending(0) && handoff.pending(1));
    handoff.complete(1,false);assert(handoff.pending(1)); // Failed release must retry.
    handoff.complete(1,true);assert(!handoff.pending(1) && handoff.pending(2));
    handoff.complete(1,true);assert(handoff.pending(2)); // A newer change cannot be lost.
    handoff.complete(2,true);assert(!handoff.pending(2));
    // A change that happened while the channel was down was never delivered.
    // Recovery must not treat it as delivered: the first frame on the rebuilt
    // channel is deliberately neutral, then the real state follows.
    handoff.complete(3,false);assert(handoff.pending(3));
    handoff.reset();
    assert(!handoff.pending(0));            // A fresh generation 0 owes nothing.
    assert(handoff.pending(3));             // The stale change still needs its release.
    handoff.complete(3,true);assert(!handoff.pending(3));
    assert(JoyConStick(1000,-2000).x==0 && JoyConStick(1000,-2000).y==0);
    assert(JoyConStick(0,0).x==0 && JoyConStick(0,0).y==0);
    assert(JoyConStick(32767,0).x==32767 && JoyConStick(0,-32768).y==-32767);
    const auto diagonal=JoyConStick(32767,32767);
    assert(diagonal.x==diagonal.y && diagonal.x>23000 && diagonal.x<23200);
    const auto partial=JoyConStick(-12000,6000);
    assert(partial.x<0 && partial.y>0 && partial.x==-2*partial.y);
}
