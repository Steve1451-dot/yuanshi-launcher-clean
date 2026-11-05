#include "endfield/entry_probe_policy.hpp"
#include <cassert>
using namespace cloudgenshin::endfield;
int main() {
    using Stage=GameBridge::Request;
    assert(AllowTitleProbe(Stage::Unknown));
    assert(AllowTitleProbe(Stage::CharacterLogout));
    assert(AllowTitleProbe(Stage::AccountLogin));
    assert(!AllowTitleProbe(Stage::CharacterLogin));
    assert(!AllowTitleProbe(Stage::BrowserOpen));
    assert(!AllowTitleProbe(Stage::AccountLogout));
    assert(!AllowTitleProbe(static_cast<Stage>(999)));
}
