#pragma once
#include "mihoyo/controller_mapping.hpp"
#include <algorithm>
#include <cmath>
#include <optional>

namespace cloudgenshin::input {
// Explicit local compatibility mode. Does not enable the native IMU gate or
// invent server feedback. Runs on the network owner at the existing input tick.
class GyroStickAssist {
public:
    SwitchControllerState apply(const ControllerMapping& config,SwitchControllerState state,
        const MotionSample& sample,std::int64_t now,bool allowed,std::optional<bool> physical_zl={}) {
        active_=false;
        if(!allowed || !config.gyro_assist || (config.gyro_aim_only && !physical_zl.value_or(state.zl)) ||
            !sample.valid || sample.source<0 || !sample.sequence || now<sample.sampled_ms ||
            now-sample.sampled_ms>100 || !FiniteMotion(sample.gyro) ||
            !FiniteMotion(sample.acceleration) || !FiniteMotion(sample.gravity) ||
            (config.gyro_sensitivity!=50 && config.gyro_sensitivity!=100 &&
             config.gyro_sensitivity!=150 && config.gyro_sensitivity!=200)) return state;
        // Android basis = {hid.z,-hid.x,-hid.y}. Landscape pitch is hid.x,
        // yaw is hid.y. Rate control (not angle integration) has no pose offset.
        const float yaw=-sample.gyro[2],pitch=-sample.gyro[1];
        constexpr float noise=0.025f; // rad/s, small stationary jitter only.
        const auto axis=[&](float rate) {
            const float magnitude=std::max(0.0f,std::abs(rate)-noise);
            // Break the remote game's stick dead zone only during real motion.
            const float value=magnitude>0 ? std::min(26000.0f,6500.0f+magnitude*14000.0f*config.gyro_sensitivity/100.0f):0;
            return static_cast<int>(std::copysign(value,rate));
        };
        // A deliberate physical stick movement wins that axis, so aiming can
        // always be overridden. Never alter left stick, buttons or triggers.
        const int x=axis(yaw)*(config.gyro_invert_x?-1:1),y=axis(pitch)*(config.gyro_invert_y?-1:1);
        if(state.right_x>=-6000 && state.right_x<=6000 && x) {state.right_x=x;active_=true;}
        if(state.right_y>=-6000 && state.right_y<=6000 && y) {state.right_y=y;active_=true;}
        if(active_) ++applied_;
        return state;
    }
    bool active() const {return active_;}
    std::uint64_t applied() const {return applied_;} // Local composition, not delivery ACK.
private:
    bool active_=false;
    std::uint64_t applied_=0;
};
} // namespace cloudgenshin::input
