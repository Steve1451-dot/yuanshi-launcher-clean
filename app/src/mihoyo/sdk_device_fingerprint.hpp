#pragma once
#include "mihoyo/gamer_login.hpp"
#include <algorithm>
#include <functional>
#include <string_view>

namespace cloudgenshin {
using DeviceFingerprintProgress=std::function<void(const std::string&,int,long)>;
// Service-issued value only. No account data/cookies, disk cache, fabricated
// fallback, dispatch or retries. Call before RTC, never from a media callback.
std::optional<std::string> FetchSdkDeviceFingerprint(const GameProfile& profile,
    const auth::WebDeviceIdentity& device,const std::atomic_bool& cancelled,const DeviceFingerprintProgress& progress);
inline bool ValidSdkDeviceFingerprint(std::string_view value) {
    return value.size()>=11 && value.size()<=64 &&
        std::all_of(value.begin(),value.end(),[](unsigned char c) {
            return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') || c=='_' || c=='-';
        });
}
}
