#pragma once

#include "mihoyo/controller_source_policy.hpp"
#include "mihoyo/input_packet.hpp"
#include "mihoyo/switch_motion_sampler.hpp"
#include <borealis/core/input.hpp>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::input {

struct ControllerSample {
    SwitchControllerState state;
    int source=-1; // -1 disconnected, 0 handheld, 1..8 complete player pads.
    bool changed=false;
    MotionSample motion;
};

// Shared by Endfield and the Genshin/Star Rail stream. Keep libnx sampling
// here so every game reads the same physical labels, sticks and source.
class ControllerSampler {
public:
    ControllerSampler() {
#ifdef __SWITCH__
        padInitialize(&handheld_,HidNpadIdType_Handheld);
        for(unsigned i=0;i<players_.size();++i)
            padInitialize(&players_[i],static_cast<HidNpadIdType>(i));
#endif
    }

    ControllerSample sample(brls::InputManager& input_manager) {
        ControllerSample result;
        auto& state=result.state;
#ifdef __SWITCH__
        (void)input_manager;
        padUpdate(&handheld_);
        std::array<bool,8> connected{};
        for(unsigned i=0;i<players_.size();++i) {
            auto& pad=players_[i];
            padUpdate(&pad);
            connected[i]=padIsNpadActive(&pad,static_cast<HidNpadIdType>(i)) &&
                (padGetStyleSet(&pad)&HidNpadStyleSet_NpadFullCtrl)!=0;
        }
        const auto selection=source_.select(padIsHandheld(&handheld_),connected);
        result.source=selection.source;result.changed=selection.changed;
        PadState* pad=result.source==0?&handheld_:
            result.source>0?&players_[static_cast<unsigned>(result.source-1)]:nullptr;
        const auto held=pad?padGetButtons(pad):0;
        result.motion=motion_.sample(result.source,pad);
#define CLOUD_BUTTON(name,button) state.name=(held&HidNpadButton_##button)!=0
        CLOUD_BUTTON(dpad_up,Up);CLOUD_BUTTON(dpad_down,Down);
        CLOUD_BUTTON(dpad_left,Left);CLOUD_BUTTON(dpad_right,Right);
        CLOUD_BUTTON(a,A);CLOUD_BUTTON(b,B);CLOUD_BUTTON(x,X);CLOUD_BUTTON(y,Y);
        CLOUD_BUTTON(l,L);CLOUD_BUTTON(r,R);CLOUD_BUTTON(zl,ZL);CLOUD_BUTTON(zr,ZR);
        CLOUD_BUTTON(plus,Plus);CLOUD_BUTTON(minus,Minus);
        CLOUD_BUTTON(stick_left,StickL);CLOUD_BUTTON(stick_right,StickR);
#undef CLOUD_BUTTON
        const auto left_raw=pad?padGetStickPos(pad,0):HidAnalogStickState{};
        const auto right_raw=pad?padGetStickPos(pad,1):HidAnalogStickState{};
        const auto left=JoyConStick(left_raw.x,left_raw.y);
        const auto right=JoyConStick(right_raw.x,right_raw.y);
#else
        brls::ControllerState fallback{};
        input_manager.updateUnifiedControllerState(&fallback);
        const bool host_connected=input_manager.getControllersConnectedCount()>0;
        std::array<bool,8> connected{};connected[0]=host_connected;
        const auto selection=source_.select(false,connected);
        result.source=selection.source;result.changed=selection.changed;
        if(!host_connected) return result;
        state.dpad_up=fallback.buttons[brls::BUTTON_UP];
        state.dpad_down=fallback.buttons[brls::BUTTON_DOWN];
        state.dpad_left=fallback.buttons[brls::BUTTON_LEFT];
        state.dpad_right=fallback.buttons[brls::BUTTON_RIGHT];
        state.a=fallback.buttons[brls::BUTTON_A];state.b=fallback.buttons[brls::BUTTON_B];
        state.x=fallback.buttons[brls::BUTTON_X];state.y=fallback.buttons[brls::BUTTON_Y];
        state.l=fallback.buttons[brls::BUTTON_LB];state.r=fallback.buttons[brls::BUTTON_RB];
        state.zl=fallback.buttons[brls::BUTTON_LT];state.zr=fallback.buttons[brls::BUTTON_RT];
        state.plus=fallback.buttons[brls::BUTTON_START];state.minus=fallback.buttons[brls::BUTTON_BACK];
        state.stick_left=fallback.buttons[brls::BUTTON_LSB];
        state.stick_right=fallback.buttons[brls::BUTTON_RSB];
        const auto axis=[](float value) {
            return std::isfinite(value)
                ? static_cast<std::int32_t>(std::lround(std::clamp(value,-1.0f,1.0f)*32767.0f)):0;
        };
        const auto left=JoyConStick(axis(fallback.axes[brls::LEFT_X]),-axis(fallback.axes[brls::LEFT_Y]));
        const auto right=JoyConStick(axis(fallback.axes[brls::RIGHT_X]),-axis(fallback.axes[brls::RIGHT_Y]));
#endif
        state.left_x=left.x;state.left_y=left.y;
        state.right_x=right.x;state.right_y=right.y;
        return result;
    }
private:
    ControllerSource source_;
    SwitchMotionSampler motion_;
#ifdef __SWITCH__
    PadState handheld_{};
    std::array<PadState,8> players_{};
#endif
};

} // namespace cloudgenshin::input
