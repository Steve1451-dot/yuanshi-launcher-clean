#pragma once

#include "atomic_file_replace.hpp"
#include "endfield/endfield_profile.hpp"
#include "endfield/wl_proto.hpp"
#include "mihoyo/stream_overlay_policy.hpp"
#include "mihoyo/stream_quality_policy.hpp"
#include "mihoyo/setting_delivery_policy.hpp"
#include <array>
#include <fstream>
#include <string>

namespace cloudgenshin::endfield {
// Row order matches the existing Genshin floating menu; this file is separate
// from its graphics-mode preference (the two protocols are not interchangeable).
using BitrateRange=cloudgenshin::StreamBitrateRange;
inline constexpr auto kStreamBitrates=cloudgenshin::kStreamQualityBitrates;
inline constexpr int kDefaultBitrateTier=0;
inline bool ValidBitrateTier(int tier) {return tier>=0 && tier<3;}
inline bool ValidStreamFps(int fps) {return fps==30 || fps==60;}
struct SavedStreamSettings {
    int tier=kDefaultBitrateTier;
    int fps=kDefaultStreamFps;
    bool valid=false;
};
inline SavedStreamSettings LoadStreamSettings(const std::string& path) {
    for(const auto& file:{path,path+".bak"}) {
        std::ifstream input(file,std::ios::binary);
        // Never scan an arbitrarily large/corrupted file on the network worker.
        if(!input || input.peek()==std::char_traits<char>::eof()) continue;
        input.seekg(0,std::ios::end);
        if(input.tellg()>64) continue;
        input.seekg(0);
        int tier=-1,fps=0;
        if(!(input>>tier>>fps) || !ValidBitrateTier(tier) || !ValidStreamFps(fps)) continue;
        input>>std::ws;
        if(input.eof()) return {tier,fps,true};
        std::string revision;
        if(!(input>>revision) || revision!="CGNX-LOW60-V1") continue;
        input>>std::ws;
        if(input.eof()) return {tier,fps,true};
    }
    return {};
}
inline bool SaveStreamSettings(const std::string& path,int tier,int fps) {
    if(!ValidBitrateTier(tier) || !ValidStreamFps(fps)) return false;
    std::ofstream output(path+".tmp",std::ios::binary|std::ios::trunc);
    if(!output) return false;
    output<<tier<<' '<<fps<<" CGNX-LOW60-V1";
    output.flush();const bool good=output.good();output.close();
    return good && !output.fail() && opennow::storage::ReplaceWithTemporaryFile(path+".tmp",path);
}

// Official setBitrateRange(min,max): lower the floor FIRST if max <= old min,
// otherwise raise/set the ceiling FIRST. SimpleCmd arg is a kbps decimal string.
inline std::array<SimpleCmd,2> BitrateRangeCommands(BitrateRange range,int previous_minimum) {
    if(range.minimum<1000 || range.maximum<range.minimum || range.maximum>kBitRateHigh) return {};
    SimpleCmd ceiling,floor;
    ceiling.cmd="ResetVideoBitrate";ceiling.arg=std::to_string(range.maximum);
    floor.cmd="autoBitrateAdjust";floor.arg=std::to_string(range.minimum);
    return range.maximum<=previous_minimum ? std::array{floor,ceiling} : std::array{ceiling,floor};
}
inline std::array<SimpleCmd,2> BitrateCommands(int tier,int previous_minimum) {
    return ValidBitrateTier(tier) ? BitrateRangeCommands(kStreamBitrates[tier],previous_minimum) : std::array<SimpleCmd,2>{};
}
template<class Send> bool SendBitrateRange(BitrateRange range,int& previous_minimum,Send&& send) {
    const auto commands=BitrateRangeCommands(range,previous_minimum);
    if(commands[0].cmd.empty()) return false;
    for(const auto& cmd:commands) if(!send(cmd.Serialize())) return false;
    previous_minimum=range.minimum;return true;
}
template<class Send> bool SendBitrateTier(int tier,int& previous_minimum,Send&& send) {
    if(!ValidBitrateTier(tier)) return false;
    return SendBitrateRange(kStreamBitrates[tier],previous_minimum,send);
}
inline std::vector<std::uint8_t> StreamFpsFrame(int fps) {
    if(!ValidStreamFps(fps)) return {};
    SimpleCmd cmd;cmd.cmd="SetFps";cmd.arg=std::to_string(fps);return cmd.Serialize();
}

// Worker-owned budget per UI revision. New explicit choices/retries get a new
// budget; exhausting initial startup must not permanently disable the menu.
using SettingSendBudget=overlay::SettingSendBudget;
} // namespace cloudgenshin::endfield
