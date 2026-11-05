#include "mihoyo/motion_input.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace cloudgenshin::input;
static constexpr MotionMatrix identity{{{1,0,0},{0,1,0},{0,0,1}}};
static void SameRotation(const MotionQuaternion& got,const MotionQuaternion& expected) {
    assert(UnitQuaternion(got));
    assert(std::abs(QuaternionDot(got,expected))>0.99999f);
}
// Independent Rodrigues construction, written to HID columns. All test angles
// and axes refer to the native sensor, not to the helper's converted matrix.
static MotionMatrix Rotation(float angle,MotionVector axis) {
    const float length=MotionLength(axis);
    for(auto& x:axis) x/=length;
    const auto [x,y,z]=axis;
    const float c=std::cos(angle),s=std::sin(angle),t=1-c;
    return {{{t*x*x+c,t*x*y+s*z,t*x*z-s*y},
             {t*x*y-s*z,t*y*y+c,t*y*z+s*x},
             {t*x*z+s*y,t*y*z-s*x,t*z*z+c}}};
}
int main() {
    constexpr float pi=3.14159265358979323846f,h=0.7071067811865475f;
    const auto neutral=AndroidOrientationFromHid(identity);
    assert(neutral);SameRotation(*neutral,{1,0,0,0});
    // Explicit off-diagonal fixtures catch a row/column transpose or inverse.
    const MotionMatrix rx90{{{1,0,0},{0,0,1},{0,-1,0}}};
    const MotionMatrix ry90{{{0,0,-1},{0,1,0},{1,0,0}}};
    const MotionMatrix rz90{{{0,1,0},{-1,0,0},{0,0,1}}};
    SameRotation(*AndroidOrientationFromHid(rx90),{h,0,-h,0});
    SameRotation(*AndroidOrientationFromHid(ry90),{h,0,0,-h});
    SameRotation(*AndroidOrientationFromHid(rz90),{h,h,0,0});
    // Exercise all trace<=0 branches, mixed axes, and both sides of pi.
    for(const auto axis:std::array<MotionVector,5>{{{1,0,0},{0,1,0},{0,0,1},{1,2,3},{-3,1,-2}}}) {
        const auto length=MotionLength(axis);
        for(int step=-720;step<=720;++step) {
            const float angle=step*pi/360;
            const auto result=AndroidOrientationFromHid(Rotation(angle,axis));
            assert(result);
            const float s=std::sin(angle/2)/length;
            SameRotation(*result,{std::cos(angle/2),axis[2]*s,-axis[0]*s,-axis[1]*s});
        }
    }
    auto rounded=rz90;rounded[0][1]+=0.001f;
    assert(AndroidOrientationFromHid(rounded));
    for(unsigned row=0;row<3;++row) for(unsigned col=0;col<3;++col) {
        auto bad=identity;bad[col][row]=std::numeric_limits<float>::quiet_NaN();
        assert(!AndroidOrientationFromHid(bad));
        bad[col][row]=std::numeric_limits<float>::infinity();
        assert(!AndroidOrientationFromHid(bad));
    }
    assert(!AndroidOrientationFromHid({}));
    auto bad=identity;bad[0][0]=-1;assert(!AndroidOrientationFromHid(bad)); // Reflection.
    bad=identity;bad[1]=bad[0];assert(!AndroidOrientationFromHid(bad));
    bad=identity;bad[1][0]=0.2f;assert(!AndroidOrientationFromHid(bad)); // Shear.
    bad=identity;bad[1][1]=2;assert(!AndroidOrientationFromHid(bad));
    assert(!UnitQuaternion({0,0,0,0}) && !UnitQuaternion({2,0,0,0}));

    MotionTracker tracker;
    const auto before=Rotation(179*pi/180,{0,0,1});
    const auto after=Rotation(181*pi/180,{0,0,1});
    auto s1=tracker.update(0,1,100,{0,1,0},{},&before);
    auto s2=tracker.update(0,2,116,{0,1,0},{},&after);
    assert(s1.orientation && s2.orientation);
    assert(QuaternionDot(*s1.orientation,*s2.orientation)>0.999f);
    assert(s2.orientation->w<0); // Same hemisphere across 180°, no sign flip.
    const auto frozen=tracker.update(0,2,300,{0,1,0},{},&identity);
    assert(frozen.sampled_ms==116 && frozen.orientation);
    SameRotation(*frozen.orientation,*s2.orientation); // Same HID sequence cannot refresh pose.
    MotionSendPolicy policy;policy.enable(true);
    assert(!policy.admit(frozen,300,16));
    auto switched=tracker.update(1,3,301,{0,1,0},{},&after);
    assert(switched.orientation && switched.orientation->w>=0); // Reset sign history at source handoff.
    auto invalid=tracker.update(1,4,317,{0,1,0},{},&bad);
    assert(invalid.valid && !invalid.orientation); // Keep gyro fallback, NEVER replay old pose.
    auto missing=tracker.update(1,5,333,{0,1,0},{});
    assert(missing.valid && !missing.orientation);
    auto recovered=tracker.update(1,6,349,{0,1,0},{},&identity);
    assert(recovered.orientation);SameRotation(*recovered.orientation,{1,0,0,0});
    recovered.orientation=MotionQuaternion{0,0,0,0};
    assert(!policy.admit(recovered,349,16));
    tracker.reset();
    assert(!tracker.update(-1,7,365,{0,1,0},{},&identity).orientation);
    std::cout<<"HID quaternion conversion: 7205 rotations, validation and freshness passed\n";
}
