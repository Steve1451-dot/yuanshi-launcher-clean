#pragma once

#include "mihoyo/input_packet.hpp"

namespace cloudgenshin::input {

// Both supported titles need pointer entry before SDK game-entry confirmation.
// Star Rail A/B testing reached login with a pointer, not repeated XInput A.
// This does not change the gameplay pad wire format or local button mappings.
inline bool ContinueViaGamepad(bool gamepad_transport,bool entered_game) {
    return gamepad_transport && entered_game;
}

inline bool FaceButtonPointerShortcut(bool entered_game) {
    return !entered_game;
}

inline std::uint32_t LauncherInputSource(bool gamepad_transport,bool touch_launcher) {
    if(!gamepad_transport) return kInputControlMouse;
    return touch_launcher ? kInputControlTouchScreen : kInputControlGamepad;
}

} // namespace cloudgenshin::input
