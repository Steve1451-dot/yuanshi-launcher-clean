#include "mihoyo/passport_signin_policy.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin::auth;
    assert(UsesPassportQr("hkrpg") && !UsesPassportQr("hk4e") && !UsesPassportQr("endfield"));
    assert(NeedsWebLoginUpdate("hkrpg",true,false));
    assert(!NeedsWebLoginUpdate("hkrpg",false,false) && !NeedsWebLoginUpdate("hkrpg",true,true));
    assert(NeedsWebLoginUpdate("hkrpg",true,true,"GAME_WEB_RESUME_FAILED"));
    assert(NeedsWebLoginUpdate("hkrpg",true,true,"GAME_WEB_RESUME_VERIFICATION_REQUIRED"));
    assert(!NeedsWebLoginUpdate("hkrpg",true,true,"GAME_WEB_RESUME_NETWORK_FAILED"));
    assert(!NeedsWebLoginUpdate("hkrpg",true,true,"GAME_WEB_RESUME_HTTP_FAILED"));
    assert(!NeedsWebLoginUpdate("hk4e",true,true,"GAME_WEB_RESUME_FAILED"));
    assert(!NeedsWebLoginUpdate("hk4e",true,false) && !NeedsWebLoginUpdate("endfield",true,false));
    assert(ValidPassportMid("synthetic-mid_123"));
    for(const auto value:{"","bad;cookie=x","bad\r\nvalue","bad value","bad/field"}) assert(!ValidPassportMid(value));
    assert(!ValidPassportMid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
}
