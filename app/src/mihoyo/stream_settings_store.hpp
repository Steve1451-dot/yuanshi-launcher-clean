#pragma once
#include "atomic_file_replace.hpp"
#include "mihoyo/stream_startup_policy.hpp"
#include <fstream>
#include <string>
#include <string_view>

namespace cloudgenshin::overlay {
// Remember the user's floating-ball choices across sessions. Values are the
// same ones the overlay sends (GraphicsMode 0..2, FPS 30/60); anything else is
// treated as "no saved value" so a damaged file cannot send an invalid request.
struct SavedSettings {
    bool valid=false;
    int graphics=0;
    int fps=0;
};

inline bool ValidGraphics(int mode) {return mode>=0 && mode<=2;}
inline bool ValidFps(int fps) {return fps==30 || fps==60;}

// Keep Genshin's existing filename; new games must not inherit or overwrite
// its preferences. Fixed names also prevent an untrusted profile becoming a path.
inline std::string StreamSettingsPath(const std::string& home,std::string_view profile) {
    if(profile=="hk4e") return home+"/cloudgenshin-stream-settings.txt";
    if(profile=="hkrpg") return home+"/hkrpg-stream-settings.txt";
    return {};
}

// Preserve explicit choices from both supported formats. Never replace a valid
// saved preference with startup defaults or rewrite the file while reading it.
inline SavedSettings LoadSavedSettings(const std::string& path) {
    if(path.empty()) return {};
    const auto parse=[&](const std::string& file) {
        std::ifstream in(file,std::ios::binary);
        if(!in) return SavedSettings{};
        in.seekg(0,std::ios::end);const auto size=in.tellg();
        if(size<0 || size>64) return SavedSettings{};
        in.seekg(0);
        int graphics=0,fps=0;
        if(!(in>>graphics>>fps)) return SavedSettings{};
        if(!ValidGraphics(graphics) || !ValidFps(fps)) return SavedSettings{};
        in>>std::ws;
        if(in.eof()) return SavedSettings{true,graphics,fps};
        std::string revision;
        if(!(in>>revision) || revision!="CGNX-LOW60-V1") return SavedSettings{};
        in>>std::ws;
        return in.eof()?SavedSettings{true,graphics,fps}:SavedSettings{};
    };
    auto saved=parse(path);
    if(!saved.valid) saved=parse(path+".bak");
    return saved;
}

// Atomic replace through a temporary file, like the language setting.
inline bool SaveSavedSettings(const std::string& path,int graphics,int fps) {
    if(path.empty() || !ValidGraphics(graphics) || !ValidFps(fps)) return false;
    std::ofstream out(path+".tmp",std::ios::binary|std::ios::trunc);
    if(!out) return false;
    out<<graphics<<' '<<fps<<" CGNX-LOW60-V1";out.flush();const bool ok=out.good();out.close();
    if(!ok || out.fail()) return false;
    return opennow::storage::ReplaceWithTemporaryFile(path+".tmp",path);
}
} // namespace cloudgenshin::overlay
