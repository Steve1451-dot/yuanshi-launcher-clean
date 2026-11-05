#pragma once
#include <string_view>
namespace cloudgenshin::auth {
inline bool UsesPassportQr(std::string_view game) {return game=="hkrpg";}
inline bool NeedsWebLoginUpdate(std::string_view game,bool signed_in,bool has_context,std::string_view status={}) {
    const bool rejected=status=="GAME_WEB_RESUME_API_REJECTED" || status=="GAME_WEB_RESUME_FAILED" ||
        status=="GAME_WEB_RESUME_VERIFICATION_REQUIRED" || status=="GAME_WEB_RESUME_ACCOUNT_MISMATCH" ||
        status=="PASSPORT_SESSION_CONTEXT_MISSING";
    return UsesPassportQr(game) && signed_in && (!has_context || rejected);
}
inline bool ValidPassportMid(std::string_view value) {
    if(value.empty() || value.size()>64) return false;
    for(const unsigned char c:value)
        if(!((c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-')) return false;
    return true;
}
}
