#include "mihoyo/controller_capture_policy.hpp"
#include "mihoyo/controller_mapping.hpp"
#include <cassert>

int main() {
    using namespace cloudgenshin::input;
    ControllerSystemButtons system;
    SwitchControllerState raw;raw.minus=true;
    system.observe(raw);auto filtered=raw;system.filter(filtered);
    assert(filtered.minus && ToGamepadState(filtered,0).buttons==0x20); // Xbox Back / map.
    DiagnosticsChord diagnostics;
    for(int now:{0,800,5000}) {
        const auto decision=diagnostics.update(raw,now);
        assert(!decision.capture && !decision.open); // Minus never opens diagnostics.
    }
    for(unsigned mask=0;mask<65536;++mask) {
        raw={};for(unsigned i=0;i<16;++i) raw.*kMappedButtons[i]=(mask&(1u<<i))!=0;
        system.reset();system.observe(raw);filtered=raw;system.filter(filtered);
        const auto before=ToGamepadState(raw,0).buttons,after=ToGamepadState(filtered,0).buttons;
        assert(after==(raw.plus && raw.minus ? before&~0x30u : before));
        for(const auto profile:{"hk4e","hkrpg","endfield"}) {
            const auto mapped=ComposeGameplayMapping(profile,DefaultControllerMapping(profile),filtered);
            assert((ToGamepadState(mapped.state,0).buttons&0x20u)==(after&0x20u));
        }
    }
    for(bool release_minus_first:{false,true}) {
        system.reset();raw={};raw.plus=raw.minus=true;system.observe(raw);
        filtered=raw;system.filter(filtered);assert(!filtered.plus && !filtered.minus);
        raw.plus=release_minus_first;raw.minus=!release_minus_first;system.observe(raw);
        filtered=raw;system.filter(filtered);assert(!filtered.plus && !filtered.minus);
        raw={};system.observe(raw);raw.minus=true;system.observe(raw);
        filtered=raw;system.filter(filtered);assert(filtered.minus); // Resume after both release.
    }
    raw={};raw.stick_left=true;
    assert(!diagnostics.update(raw,6000).capture);
    raw.stick_right=true;
    assert(diagnostics.update(raw,6010).capture);
    assert(!diagnostics.update(raw,6809).open);
    assert(diagnostics.update(raw,6810).open);
    assert(!diagnostics.update(raw,9000).open); // One toggle per complete gesture.
    raw.stick_left=false;
    assert(diagnostics.update(raw,9001).capture); // Partial release remains captured.
    raw={};assert(!diagnostics.update(raw,9002).capture);
    raw.stick_left=raw.stick_right=true;
    assert(diagnostics.update(raw,10000).capture && diagnostics.update(raw,10800).open);
    assert(!diagnostics.update(raw,11000,false).capture); // Disabled/reset on UI/source transitions.

    // Endfield's full capture path also forwards standalone Back, withholds
    // the pair while the local gesture owns it, and requires a neutral release.
    ControllerCapture capture;capture.update({},0,false);
    raw={};raw.minus=true;auto frame=capture.update(raw,0,false);
    assert(frame.state.minus && ToGamepadState(frame.state,0).buttons==0x20);
    frame=capture.update(raw,0,true);assert(ControlsReleased(frame.state));
    frame=capture.update(raw,0,false);assert(ControlsReleased(frame.state));
    capture.update({},0,false);frame=capture.update(raw,0,false);
    assert(frame.state.minus);
}
