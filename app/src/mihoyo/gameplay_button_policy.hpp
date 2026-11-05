#pragma once
#include "mihoyo/input_packet.hpp"
#include <string_view>
#include <utility>

namespace cloudgenshin::input {

// Fixed user-requested Genshin-only physical face-button swap. The caller
// applies this to a copy for remote gameplay, never the shared HID/UI state.
// Combo app 4 identifies Genshin; do not change Star Rail (8), unknown apps,
// Endfield's separate route, or unknown profiles. Synthetic Continue/entry
// must be composed AFTER this so it remains the intended cloud XInput A.
inline SwitchControllerState GameplayFaceButtons(std::string_view app_id,SwitchControllerState state) {
    if(app_id=="4") {
        std::swap(state.a,state.b);
        std::swap(state.x,state.y);
    }
    return state;
}

} // namespace cloudgenshin::input
