#pragma once
#include "endfield/game_bridge.hpp"

namespace cloudgenshin::endfield {
// This is ONLY a separate, exact-marker, one-shot test probe armed after an
// operator inspects a current decoded title frame. It is not normal input or
// an autonomous screen recognizer. An account can already be signed in while
// the verified title still says "click anywhere to continue".
inline bool AllowTitleProbe(GameBridge::Request stage) {
    return stage==GameBridge::Request::Unknown || stage==GameBridge::Request::CharacterLogout ||
           stage==GameBridge::Request::AccountLogin;
}
} // namespace cloudgenshin::endfield
