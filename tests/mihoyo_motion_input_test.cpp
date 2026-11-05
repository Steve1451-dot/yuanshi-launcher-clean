#include "mihoyo/motion_input.hpp"
#include "mihoyo/input_packet.hpp"
#include "mihoyo/game_control.hpp"
#include "mihoyo/controller_capture_policy.hpp"
#include <cassert>
#include <limits>
#include <iostream>

using namespace cloudgenshin;
int main() {
    input::MotionTracker tracker;
    auto sample=tracker.update(0,1,100,{0,1,0},{0.25f,0.5f,1});
    assert(sample.valid && sample.source==0 && sample.gravity[2]==1);
    assert(std::abs(sample.acceleration[2]-9.80665f)<0.00001f);
    assert(std::abs(sample.gyro[0]-6.2831853f)<0.00001f);
    assert(std::abs(sample.gyro[1]+1.5707963f)<0.00001f);
    assert(std::abs(sample.gyro[2]+3.1415927f)<0.00001f);
    assert(tracker.update(0,1,500,{0,1,0},{}).sampled_ms==100);
    assert(!tracker.update(1,2,501,{0,0,0},{}).valid);
    assert(!tracker.update(1,3,502,{0,1,0},{0,0,std::numeric_limits<float>::infinity()}).valid);
    assert(!tracker.update(1,4,503,{0,1,0},{100,0,0}).valid);
    assert(tracker.update(1,5,504,{1,0,0},{}).valid);
    assert(!tracker.update(1,6,700,{0,0,0},{}).valid); // Restart after a gap.
    input::MotionSendPolicy policy;
    assert(!policy.admit(sample,100,16));
    policy.enable(true);
    assert(policy.admit(sample,100,16));policy.failed();
    assert(!policy.admit(sample,116,16)); // No replay on failure.
    sample.sequence++;sample.sampled_ms=110;
    assert(!policy.admit(sample,110,16));
    assert(policy.admit(sample,116,16));policy.sent(sample);
    sample.sequence++;sample.sampled_ms=132;
    assert(!policy.admit(sample,132,34,true)); // Gravity hasn't changed.
    assert(!policy.admit(sample,500,16)); // Stale, even with a new sequence.
    sample.sampled_ms=501;assert(!policy.admit(sample,500,16)); // Future timestamp.
    sample.sampled_ms=500;sample.gravity={0,1,0};
    assert(policy.admit(sample,500,34,true));policy.sent(sample);
    assert(policy.sent_count()==2 && policy.failure_count()==1);
    policy.enable(false);assert(!policy.admit(sample,600,16));
    policy.enable(true);sample.sampled_ms=600;
    assert(policy.admit(sample,600,16)); // A real new activation resets the gate.

    // Independent golden bytes: type 22 / nested field 14, fixed32 floats.
    sample.gyro={1,0,0};sample.gravity={0,0,1};sample.acceleration={0,0,2};
    const std::vector<std::uint8_t> expected{8,22,114,18,
        13,0,0,128,63,77,0,0,128,191,101,0,0,0,64,160,1,1};
    assert(input::EncodeImuChannel(sample)==expected);
    sample.orientation=input::MotionQuaternion{0.5f,0.5f,0.5f,0.5f};
    const std::vector<std::uint8_t> pose_expected{8,22,114,39,
        13,0,0,128,63,77,0,0,128,191,101,0,0,0,64,
        109,0,0,0,63,117,0,0,0,63,125,0,0,0,63,133,1,0,0,0,63,160,1,1};
    assert(input::EncodeImuChannel(sample)==pose_expected);
    sample.orientation=input::MotionQuaternion{0.5f,-0.5f,0,0.7071067811865475f};
    const std::vector<std::uint8_t> asymmetric_expected{8,22,114,34,
        13,0,0,128,63,77,0,0,128,191,101,0,0,0,64,
        109,0,0,0,63,117,0,0,0,191,133,1,243,4,53,63,160,1,1};
    assert(input::EncodeImuChannel(sample)==asymmetric_expected); // All four components distinct.
    sample.orientation=input::MotionQuaternion{1,0,0,0};
    const std::vector<std::uint8_t> identity_expected{8,22,114,23,
        13,0,0,128,63,77,0,0,128,191,101,0,0,0,64,109,0,0,128,63,160,1,1};
    assert(input::EncodeImuChannel(sample)==identity_expected); // Field 13 is APK w, not x.
    sample.orientation=input::MotionQuaternion{0,0,0,0};
    assert(input::EncodeImuChannel(sample).empty());
    sample.orientation=input::MotionQuaternion{1,0,0,std::numeric_limits<float>::infinity()};
    assert(input::EncodeImuChannel(sample).empty());
    sample.orientation.reset();
    sample.gyro[0]=std::numeric_limits<float>::quiet_NaN();
    assert(input::EncodeImuChannel(sample).empty());
    assert(control::DecodeImuFeedback(std::vector<std::uint8_t>{8,23,122,2,8,1})==true);
    assert(control::DecodeImuFeedback(std::vector<std::uint8_t>{8,23,122,0})==false);
    for(const auto& bad:std::vector<std::vector<std::uint8_t>>{
        {8,22,122,2,8,1},{8,23},{8,23,122,2,8,2},{8,23,122,2,8},
        {8,23,122,4,8,1,8,0},{8,23,8,23,122,0},{8,23,122,0,122,0},
        {8,23,122,0,0},{8,23,122,0,128}}) assert(!control::DecodeImuFeedback(bad));
    const std::vector<std::uint8_t> complete{8,23,122,2,8,1};
    for(std::size_t n=0;n<complete.size();++n)
        assert(!control::DecodeImuFeedback(std::span(complete).first(n)));
    input::ControllerCapture capture;
    assert(!capture.motion_allowed());
    capture.update({},0,false);assert(capture.motion_allowed());
    capture.update({},0,true);assert(!capture.motion_allowed());
    capture.update({},0,false);assert(capture.motion_allowed());
    capture.arm();assert(!capture.motion_allowed());
    capture.update({},-1,false);assert(!capture.motion_allowed());
    std::cout<<"native motion protocol/policy passed\n";
}
