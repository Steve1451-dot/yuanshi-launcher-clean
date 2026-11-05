#pragma once
#include <fstream>
#include <string>
#include <string_view>

namespace cloudgenshin {
// Explicit user consent, not an autotest or release default. This marker must
// only be created after the device-data destination/purpose has been disclosed.
inline bool OfficialDeviceInfoAllowed(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    char bytes[64]{};file.read(bytes,sizeof(bytes));
    return file.eof() && std::string_view(bytes,static_cast<std::size_t>(file.gcount()))==
        "ALLOW_OFFICIAL_MIHOYO_DEVICE_INFO_V1\n";
}
}
