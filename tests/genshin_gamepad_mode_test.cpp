#include "mihoyo/genshin_gamepad_mode.hpp"
#include "mihoyo/controller_mapping.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>

int main() {
    using namespace cloudgenshin::input;
    const auto directory=std::filesystem::temp_directory_path()/
        ("yuanshi-native-pad-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);const auto home=directory.string();
    const auto path=GenshinGamepadModePath(home);
    auto loaded=LoadGenshinGamepadMode(home);
    assert(!loaded.restored && loaded.mode==GenshinGamepadMode::Native);
    for(const auto profile:{"hk4e","hkrpg","endfield","","../hk4e"}) {
        assert(NativeGamepadForProfile(profile,GenshinGamepadMode::Native)==(std::string_view(profile)=="hk4e"));
        assert(!NativeGamepadForProfile(profile,GenshinGamepadMode::LocalMapping));
    }
    auto custom=DefaultControllerMapping("hk4e");custom.target.fill(kDisabledButton);custom.target[kFixedMinusButton]=kFixedMinusButton;
    assert(SaveControllerMapping(home,"hk4e",custom));
    {std::ofstream out(directory/"cloudgenshin-terminal.txt");out<<"6";}
    {std::ofstream out(directory/"genshin-input-mode.txt");out<<"CGNX-INPUT 1 standard";}
    for(const auto mode:{GenshinGamepadMode::LocalMapping,GenshinGamepadMode::Native}) {
        assert(SaveGenshinGamepadMode(home,mode));loaded=LoadGenshinGamepadMode(home);
        assert(loaded.restored && loaded.mode==mode);
        assert(LoadControllerMapping(home,"hk4e").value==custom);
        for(const auto& name:{"cloudgenshin-terminal.txt","genshin-input-mode.txt"}) {
            std::ifstream in(directory/name);const std::string value((std::istreambuf_iterator<char>(in)),{});
            assert(value==(std::string_view(name)=="cloudgenshin-terminal.txt"?"6":"CGNX-INPUT 1 standard"));
        }
    }
    // Native mode ignores even a completely different local table, for every
    // button, trigger and stick. Only the existing Xbox wire encoder applies.
    for(auto button:kMappedButtons) {
        SwitchControllerState raw;raw.*button=true;
        raw.left_x=-31000;raw.left_y=28000;raw.right_x=18000;raw.right_y=-9000;
        const auto frame=ComposeGameplayMapping("hk4e",custom,raw,true);
        assert(frame.native_navigation && !frame.gyro_allowed && !frame.wheel_navigation);
        assert(EncodeGamepadChannel(ToGamepadState(frame.state,100))==EncodeGamepadChannel(ToGamepadState(raw,100)));
    }
    const auto native=ComposeGameplayMapping("hk4e",DefaultControllerMapping("hk4e"),{.r=true},true);
    const auto local=ComposeGameplayMapping("hk4e",DefaultControllerMapping("hk4e"),{.r=true},false);
    assert(ToGamepadState(native.state,100).buttons!=ToGamepadState(local.state,100).buttons);
    assert(SaveGenshinGamepadMode(home,GenshinGamepadMode::LocalMapping));
    std::filesystem::copy_file(path,path+".bak");
    {std::ofstream out(path);out<<"broken";}
    assert(LoadGenshinGamepadMode(home).mode==GenshinGamepadMode::LocalMapping);
    // Explicit native choice takes priority over a valid local backup.
    {std::ofstream out(path);out<<"YUANSHI-GAMEPAD 1 native\n";}
    assert(LoadGenshinGamepadMode(home).mode==GenshinGamepadMode::Native);
    std::filesystem::remove(path+".bak");
    for(const auto& bad:{std::string("YUANSHI-GAMEPAD 2 local"),std::string("YUANSHI-GAMEPAD 1 local trailing"),
        std::string("YUANSHI-GAMEPAD 1 cloud-saved"),std::string(65,'x')}) {
        {std::ofstream out(path);out<<bad;}
        loaded=LoadGenshinGamepadMode(home);assert(!loaded.restored && loaded.mode==GenshinGamepadMode::Native);
    }
    assert(!SaveGenshinGamepadMode(home,static_cast<GenshinGamepadMode>(99)));
    assert(!SaveGenshinGamepadMode((directory/"missing").string(),GenshinGamepadMode::Native));
    std::filesystem::remove_all(directory);
}
