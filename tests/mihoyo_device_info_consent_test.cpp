#include "mihoyo/device_info_consent.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
int main() {
    const auto path=std::filesystem::temp_directory_path()/
        ("cg-device-consent-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".txt");
    assert(!std::filesystem::exists(path));
    assert(!cloudgenshin::OfficialDeviceInfoAllowed(path.string()));
    const auto check=[&](const std::string& value,bool allowed) {
        {std::ofstream file(path,std::ios::binary);file<<value;}
        assert(cloudgenshin::OfficialDeviceInfoAllowed(path.string())==allowed);
    };
    check("",false);check("yes",false);check("CGNX_AUTOTEST_V1\n",false);
    check("ALLOW_OFFICIAL_MIHOYO_DEVICE_INFO_V1\n",true);
    check("ALLOW_OFFICIAL_MIHOYO_DEVICE_INFO_V1\nextra",false);
    check(std::string(65,'x'),false);
    assert(std::filesystem::remove(path));
}
