#pragma once
#include "atomic_file_replace.hpp"
#include <fstream>
#include <string>
#include <string_view>

namespace cloudgenshin::input {
enum class GenshinGamepadMode {Native,LocalMapping};
struct SavedGenshinGamepadMode {GenshinGamepadMode mode=GenshinGamepadMode::Native;bool restored=false;};
inline std::string GenshinGamepadModePath(const std::string& home) {return home+"/genshin-gamepad-mode.txt";}
inline bool NativeGamepadForProfile(std::string_view profile,GenshinGamepadMode mode) {
    return profile=="hk4e" && mode==GenshinGamepadMode::Native;
}
// Client routing preference only. This never claims to read/write the cloud
// game's action bindings, nor changes terminal, pad identity or saved mappings.
inline SavedGenshinGamepadMode LoadGenshinGamepadMode(const std::string& home) {
    const auto path=GenshinGamepadModePath(home);
    for(const auto& file:{path,path+".bak"}) {
        std::ifstream in(file,std::ios::binary);if(!in) continue;
        in.seekg(0,std::ios::end);const auto size=in.tellg();if(size<0 || size>64) continue;
        in.seekg(0);std::string magic,mode;int revision=0;
        if(!(in>>magic>>revision>>mode) || magic!="YUANSHI-GAMEPAD" || revision!=1) continue;
        in>>std::ws;if(!in.eof()) continue;
        if(mode=="native") return {GenshinGamepadMode::Native,true};
        if(mode=="local") return {GenshinGamepadMode::LocalMapping,true};
    }
    return {};
}
inline bool SaveGenshinGamepadMode(const std::string& home,GenshinGamepadMode mode) {
    if(mode!=GenshinGamepadMode::Native && mode!=GenshinGamepadMode::LocalMapping) return false;
    const auto path=GenshinGamepadModePath(home);
    std::ofstream out(path+".tmp",std::ios::binary|std::ios::trunc);if(!out) return false;
    out<<"YUANSHI-GAMEPAD 1 "<<(mode==GenshinGamepadMode::Native?"native":"local")<<'\n';
    out.flush();const bool good=out.good();out.close();
    return good && !out.fail() && opennow::storage::ReplaceWithTemporaryFile(path+".tmp",path);
}
} // namespace cloudgenshin::input
