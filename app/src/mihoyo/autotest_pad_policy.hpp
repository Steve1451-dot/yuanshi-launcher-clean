#pragma once
#include "mihoyo/input_packet.hpp"
#include <cstdint>
#include <fstream>
#include <string>

namespace cloudgenshin {
inline bool AutoTestPadEnabled(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    char bytes[32]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_PAD_PROBE_V1\n";
}
// A bounded, opt-in camera/menu probe, not gameplay automation. Apply before
// overlay/release handling so it traverses the same route as physical input.
// Never moves the character, attacks, confirms a dialog, purchases or chats.
class AutoTestPadProbe {
public:
    struct Sample {input::SwitchControllerState state{};int phase=0;};
    void arm(std::int64_t game_enter_ms) {if(start_ms_==0 && game_enter_ms>0) start_ms_=game_enter_ms;}
    Sample sample(std::int64_t now_ms) const {
        if(start_ms_==0 || now_ms<start_ms_) return {};
        const auto elapsed=now_ms-start_ms_;
        Sample result;
        if(elapsed<15000) return result;
        if(elapsed<17000) {result.phase=1;result.state.right_x=20000;}
        else if(elapsed<20000) result.phase=2;
        else if(elapsed<20200) {result.phase=3;result.state.plus=true;}
        else if(elapsed<26000) result.phase=4;
        // Confirm/cancel can be swapped in the user's game settings. Stop at
        // the menu instead of guessing a face button that could confirm Shop.
        else if(elapsed<26200) result.phase=5;
        else result.phase=6;
        return result;
    }
private:
    std::int64_t start_ms_=0;
};
}
