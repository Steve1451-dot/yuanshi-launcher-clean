#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cloudgenshin::input {
// One local complete pad -> one cloud XInput pad. Never OR an idle external
// controller into the attached Joy-Con pair or combine sticks across players.
class ControllerSource {
public:
    struct Selection {int source=-1;bool changed=false;}; // 0 handheld, 1..8 players.
    Selection select(bool handheld,const std::array<bool,8>& players) {
        int next=-1;
        if(handheld) next=0;
        else if(source_>0 && players[static_cast<unsigned>(source_-1)]) next=source_;
        else {
            for(unsigned i=0;i<players.size();++i) if(players[i]) {next=static_cast<int>(i)+1;break;}
        }
        const bool changed=next!=source_;
        source_=next;
        return {source_,changed};
    }
private:
    int source_=-1;
};
class ControllerNeutralHandoff {
public:
    bool pending(unsigned generation) const {return delivered_!=generation;}
    void complete(unsigned generation,bool sent) {if(sent) delivered_=generation;}
    // Channel recovery restarts the delivery bookkeeping: the pod forgot every
    // previous frame, so a pending neutral must not stay pending forever.
    void reset() {delivered_=0;}
private:
    unsigned delivered_=0;
};
struct StickAxes {std::int32_t x=0,y=0;};
// Same 12% radial deadzone as the shared streamer. No smoothing/extra frame
// buffering: preserve direction and rescale the remaining travel to full range.
inline StickAxes JoyConStick(std::int32_t x,std::int32_t y) {
    constexpr double maximum=32767.0,deadzone=maximum*0.12;
    const double ax=std::clamp(x,-32767,32767),ay=std::clamp(y,-32767,32767);
    const double magnitude=std::hypot(ax,ay);
    if(magnitude<=deadzone) return {};
    const double scale=std::min(1.0,(magnitude-deadzone)/(maximum-deadzone))*maximum/magnitude;
    return {static_cast<std::int32_t>(std::lround(ax*scale)),
            static_cast<std::int32_t>(std::lround(ay*scale))};
}
}
