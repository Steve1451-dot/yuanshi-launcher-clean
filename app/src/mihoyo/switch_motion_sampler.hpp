#pragma once
#include "mihoyo/motion_input.hpp"
#include <chrono>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::input {
// Borrow the sensors already started/stopped by Borealis. Do not restart or
// stop its shared handles when a stream view is created/destroyed. Like the
// Moonlight path this supports handheld and the primary complete controller.
class SwitchMotionSampler {
public:
#ifdef __SWITCH__
    SwitchMotionSampler() {
        available_[0]=R_SUCCEEDED(hidGetSixAxisSensorHandles(&handles_[0],1,HidNpadIdType_Handheld,HidNpadStyleTag_NpadHandheld));
        available_[1]=R_SUCCEEDED(hidGetSixAxisSensorHandles(&handles_[1],1,HidNpadIdType_No1,HidNpadStyleTag_NpadFullKey));
        const bool dual=R_SUCCEEDED(hidGetSixAxisSensorHandles(&handles_[2],2,HidNpadIdType_No1,HidNpadStyleTag_NpadJoyDual));
        available_[2]=available_[3]=dual;
    }
    MotionSample sample(int source,PadState* pad) {
        int sensor=-1;
        if(pad && source==0) sensor=0;
        else if(pad && source==1) {
            const auto style=padGetStyleSet(pad),attributes=padGetAttributes(pad);
            if(style&HidNpadStyleTag_NpadFullKey) sensor=1;
            else if(style&HidNpadStyleTag_NpadJoyDual)
                sensor=attributes&HidNpadAttribute_IsLeftConnected ? 2 :
                       attributes&HidNpadAttribute_IsRightConnected ? 3 : -1;
        }
        if(sensor!=sensor_) {tracker_.reset();sensor_=sensor;}
        HidSixAxisSensorState raw{};
        if(sensor<0 || !available_[sensor] ||
           hidGetSixAxisSensorStates(handles_[sensor],&raw,1)!=1 ||
           !(raw.attributes&HidSixAxisSensorAttribute_IsConnected)) {tracker_.reset();return {};}
        const auto now=std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        MotionMatrix direction{};
        for(unsigned column=0;column<3;++column) for(unsigned row=0;row<3;++row)
            direction[column][row]=raw.direction.direction[column][row];
        return tracker_.update(source,raw.sampling_number,now,
            {raw.acceleration.x,raw.acceleration.y,raw.acceleration.z},
            {raw.angular_velocity.x,raw.angular_velocity.y,raw.angular_velocity.z},&direction);
    }
#else
    MotionSample sample(int,void*) {return {};}
#endif
private:
    MotionTracker tracker_;
    int sensor_=-1;
#ifdef __SWITCH__
    HidSixAxisSensorHandle handles_[4]{};
    bool available_[4]{};
#endif
};
} // namespace cloudgenshin::input
