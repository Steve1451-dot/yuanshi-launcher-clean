#include "mihoyo/gyro_stick_assist.hpp"
#include <cassert>
#include <limits>
#include <iostream>
using namespace cloudgenshin::input;
int main() {
    MotionTracker tracker;
    const auto sample=tracker.update(0,1,100,{0,1,0},{0.10f,0.20f,0.30f});
    assert(sample.valid);
    GyroStickAssist gyro;
    auto config=DefaultControllerMapping("hk4e");
    assert(config.gyro_assist && config.gyro_aim_only && config.gyro_invert_x);
    assert(!DefaultControllerMapping("hkrpg").gyro_assist && !DefaultControllerMapping("endfield").gyro_assist);
    config.gyro_assist=false;
    SwitchControllerState raw;raw.zl=true;raw.a=true;raw.left_x=12000;
    assert(gyro.apply(config,raw,sample,100,true).right_x==0 && !gyro.active());
    config.gyro_assist=true;
    const auto result=gyro.apply(config,raw,sample,100,true);
    assert(result.right_x< -6000 && result.right_y>6000 && gyro.active());
    assert(result.left_x==raw.left_x && result.zl && result.a && raw.right_x==0);
    // The real ZL is the activation key, not whichever source maps to game LT.
    auto no_game_lt=raw;no_game_lt.zl=false;
    assert(gyro.apply(config,no_game_lt,sample,100,true,true).right_x==result.right_x);
    assert(gyro.apply(config,raw,sample,100,true,false).right_x==0 && !gyro.active());
    assert(gyro.apply(config,no_game_lt,sample,100,true,false).right_y==0);
    const auto count=gyro.applied();
    config.gyro_invert_x=false;config.gyro_invert_y=true;
    const auto inverted=gyro.apply(config,raw,sample,100,true);
    assert(inverted.right_x==-result.right_x && inverted.right_y==-result.right_y);
    config.gyro_invert_x=true;config.gyro_invert_y=false;
    raw.right_x=21000;raw.right_y=-22000;
    const auto override=gyro.apply(config,raw,sample,100,true);
    assert(override.right_x==21000 && override.right_y==-22000 && !gyro.active());
    raw.right_x=raw.right_y=0;raw.zl=false;
    assert(gyro.apply(config,raw,sample,100,true).right_x==0);
    config.gyro_aim_only=false;
    assert(gyro.apply(config,raw,sample,100,true).right_x!=0);
    assert(gyro.apply(config,raw,sample,201,true).right_x==0 && !gyro.active());
    assert(gyro.apply(config,raw,sample,99,true).right_x==0);
    assert(gyro.apply(config,raw,sample,100,false).right_x==0); // UI capture / handoff / no transport.
    auto bad=sample;bad.valid=false;assert(gyro.apply(config,raw,bad,100,true).right_x==0);
    bad=sample;bad.gyro[2]=std::numeric_limits<float>::infinity();assert(gyro.apply(config,raw,bad,100,true).right_x==0);
    bad=sample;bad.sequence=0;assert(gyro.apply(config,raw,bad,100,true).right_x==0);
    bad=sample;bad.source=-1;assert(gyro.apply(config,raw,bad,100,true).right_x==0);
    auto still=tracker.update(0,2,116,{0,1,0},{0,0,0});
    assert(gyro.apply(config,raw,still,116,true).right_x==0 && !gyro.active());
    const auto noisy=tracker.update(0,3,132,{0,1,0},{0.001f,0.001f,0.001f});
    assert(gyro.apply(config,raw,noisy,132,true).right_x==0 && !gyro.active());
    const auto fast=tracker.update(0,4,148,{0,1,0},{20,20,20});
    const auto limited=gyro.apply(config,raw,fast,148,true);
    assert(limited.right_x==-26000 && limited.right_y==26000);
    const auto reverse=tracker.update(0,5,164,{0,1,0},{-20,-20,-20});
    const auto reversed=gyro.apply(config,raw,reverse,164,true);
    assert(reversed.right_x==26000 && reversed.right_y==-26000);
    raw.right_x=std::numeric_limits<int>::min();
    assert(gyro.apply(config,raw,fast,148,true).right_x==raw.right_x);
    config.gyro_sensitivity=51;raw.right_x=0;
    assert(gyro.apply(config,raw,sample,100,true).right_x==0);
    assert(gyro.applied()>count);
    // Physical ZL is intentionally retained for the new position preset.
    // Attack R alone must never activate gyro, and L/menu context blocks even
    // a saved Always preference. The real caller composes this same policy.
    config=DefaultControllerMapping("hk4e");raw={};raw.r=true;
    auto frame=ComposeGameplayMapping("hk4e",config,raw);
    assert(gyro.apply(config,frame.state,sample,100,frame.gyro_allowed,raw.zl).right_x==0);
    raw.zl=true;frame=ComposeGameplayMapping("hk4e",config,raw);
    assert(gyro.apply(config,frame.state,sample,100,frame.gyro_allowed,raw.zl).right_x< -6000);
    raw.l=true;frame=ComposeGameplayMapping("hk4e",config,raw);
    assert(frame.wheel_navigation && !frame.gyro_allowed);
    assert(gyro.apply(config,frame.state,sample,100,frame.gyro_allowed,raw.zl).right_x==0);
    config.gyro_aim_only=false;raw.l=false;
    frame=ComposeGameplayMapping("hk4e",config,raw,true);
    assert(gyro.apply(config,frame.state,sample,100,frame.gyro_allowed,raw.zl).right_x==0 && !gyro.active());
    frame=ComposeGameplayMapping("hk4e",config,{});
    assert(gyro.apply(config,frame.state,{},100,false,false).right_x==0 && !gyro.active());
    std::cout<<"PASS local gyro rate-to-stick: gating, freshness, axes, inversion, noise, physical override, clamps\n";
}
