#include "mihoyo/launcher_input_policy.hpp"
#include "mihoyo/stream_input_policy.hpp"
#include <cassert>

int main() {
    using namespace cloudgenshin::input;
    // Shared Genshin/Star Rail entry uses the pointer; gameplay Continue is
    // still gamepad-only after the SDK entry signal, never both input paths.
    for(const bool pad:{false,true}) {
        assert(!ContinueViaGamepad(pad,false));
        assert(ContinueViaGamepad(pad,true)==pad);
        assert(FaceButtonPointerShortcut(false));
        assert(!FaceButtonPointerShortcut(true));
        assert(LauncherInputSource(pad,true)==(pad?kInputControlTouchScreen:kInputControlMouse));
        assert(LauncherInputSource(pad,false)==(pad?kInputControlGamepad:kInputControlMouse));
    }
    // Continue still maps to one held XInput A (Switch B), and the next
    // neutral frame releases it; physical A/B mappings are not swapped.
    SwitchControllerState physical;
    physical.a=true;
    const auto held=ComposeGamepadFrame(physical,true,false);
    assert(held.b && !held.a);
    assert(ControlsReleased(ComposeGamepadFrame({},false,false)));
}
