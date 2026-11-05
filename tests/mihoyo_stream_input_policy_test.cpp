#include "mihoyo/stream_input_policy.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin::input;
    assert(!UsesTouchLauncher(5) && !UsesTouchLauncher(10) && !UsesTouchLauncher(1));
    assert(UsesTouchLauncher(3) && UsesTouchLauncher(9) && UsesTouchLauncher(2));
    DeliveredButton key;int calls=0;
    auto fail=[&](bool){++calls;return false;};auto pass=[&](bool){++calls;return true;};
    assert(!key.apply(true,fail) && !key.held());
    assert(key.apply(true,pass) && key.held());
    assert(key.apply(true,pass) && calls==2); // A successful held key is not spammed.
    assert(!key.apply(false,fail) && key.held());
    assert(key.apply(false,pass) && !key.held());
    QualityMenuChord chord;
    assert(!chord.update(true,false,0).capture);
    assert(chord.update(true,true,10).capture);
    assert(!chord.update(true,true,809).open);
    assert(chord.update(true,true,810).open);
    assert(!chord.update(true,true,2000).open);
    assert(chord.update(false,true,2001).capture);
    assert(!chord.update(false,false,2002).capture);
    QualityMenuChord cancelled;
    assert(cancelled.update(true,true,0).capture);
    assert(cancelled.update(true,false,10).capture);
    assert(!cancelled.update(true,true,1000).open);
    assert(!cancelled.update(false,false,1001).capture);
    assert(cancelled.update(true,true,1100).capture && cancelled.update(true,true,1900).open);
    SwitchControllerState state;assert(ControlsReleased(state));state.b=true;assert(!ControlsReleased(state));
    state={};state.right_x=10000;assert(!ControlsReleased(state));
    // Synthetic A must be re-applied on every frame of its hold window.
    SwitchControllerState physical;physical.a=true;physical.x=true;physical.left_x=9000;
    const auto untouched=ComposeGamepadFrame(physical,false,false);
    assert(untouched.a && untouched.x && untouched.left_x==9000);
    const auto confirmed=ComposeGamepadFrame(physical,true,false);
    assert(confirmed.b && !confirmed.a && !confirmed.x && confirmed.left_x==0);
    const auto auto_entry=ComposeGamepadFrame(physical,false,true);
    assert(auto_entry.b && !auto_entry.a && !auto_entry.x);
    assert(ControlsReleased(ComposeGamepadFrame(physical,false,false))==false);
    assert(!ControlsReleased(auto_entry));
    // Release gate: a stale touch contact must never mute the controller. Only
    // control state and the bounded timeout decide, and the timeout releases.
    InputReleaseGate gate;
    assert(gate.pending());
    SwitchControllerState neutral;
    assert(!gate.suppress(neutral,0) && !gate.pending()); // Neutral releases.
    InputReleaseGate held;
    SwitchControllerState pressed; pressed.a=true;
    assert(held.suppress(pressed,0) && held.pending());   // Held: still gated.
    assert(held.suppress(pressed,InputReleaseGate::kTimeoutMs-1) && held.pending());
    assert(!held.suppress(pressed,InputReleaseGate::kTimeoutMs+1) && !held.pending());
    // Re-arming (menu opened again) gates the next frames again.
    held.arm(5000);
    assert(held.pending() && held.suppress(pressed,5000));
    assert(!held.suppress(neutral,5001) && !held.pending());
    ManualContinueClick click;using Action=ManualContinueClick::Action;
    assert(click.next(999)==Action::None); // Frames/time alone never click.
    assert(click.request(1000) && !click.request(1001));
    assert(click.next(1000)==Action::Move);assert(!click.sent(false,1000));
    assert(click.next(1099)==Action::None && click.next(1100)==Action::Move);
    assert(!click.sent(true,1100) && click.next(1160)==Action::Down);
    assert(!click.sent(false,1160) && click.next(1260)==Action::Down);
    assert(!click.sent(true,1260) && click.next(1350)==Action::Up);
    assert(!click.sent(false,1350) && click.next(1450)==Action::Up);
    assert(click.sent(true,1450) && !click.active() && click.next(10000)==Action::None);
    assert(click.request(10000));assert(click.next(12000)==Action::Failed && !click.active());
}
