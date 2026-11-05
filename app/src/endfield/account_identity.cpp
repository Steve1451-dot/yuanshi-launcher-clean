#include "endfield/account_policy.hpp"
#include <array>
#include <random>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::endfield::account {
const std::string& SessionDeviceId() {
    // Shared across QR creation, confirmation, OAuth grant and binding calls.
    // Thread-safe, process-lifetime app identity; never a hardware/account id.
    static const std::string id=[] {
        std::array<unsigned char,16> bytes{};
#ifdef __SWITCH__
        randomGet(bytes.data(),bytes.size());
#else
        std::random_device random;
        for(auto& byte:bytes) byte=static_cast<unsigned char>(random());
#endif
        bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
        constexpr char hex[]="0123456789abcdef";std::string value;
        for(const auto byte:bytes) {value+=hex[byte>>4];value+=hex[byte&15];}
        return value;
    }();
    return id;
}
std::vector<std::string> SessionHeaders(bool json_body) {
    return Headers(SessionDeviceId(),json_body);
}
} // namespace cloudgenshin::endfield::account
