#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace cloudgenshin::input {
// libnx DirectionState stores three basis COLUMNS, not matrix rows. See
// switchbrew/switch-examples hid/sixaxis's direction-matrix display.
using MotionMatrix = std::array<std::array<float,3>,3>;
struct MotionQuaternion {
    float w=1,x=0,y=0,z=0; // Android getQuaternionFromVector order, not wire names.
};
inline float QuaternionDot(const MotionQuaternion& a,const MotionQuaternion& b) {
    return a.w*b.w+a.x*b.x+a.y*b.y+a.z*b.z;
}
inline bool UnitQuaternion(const MotionQuaternion& q) {
    return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) &&
        std::isfinite(q.z) && std::abs(QuaternionDot(q,q)-1.0f)<=0.01f;
}
inline MotionQuaternion NegateQuaternion(const MotionQuaternion& q) {
    return {-q.w,-q.x,-q.y,-q.z};
}

// Device->reference rotation in the same natural-portrait basis as the gyro:
// C*v = {v.z,-v.x,-v.y}. C is a proper rotation. Convert BOTH bases, R=C*D*C^T;
// merely shuffling quaternion components, or transposing the libnx matrix a
// second time, reverses axes/directions. This preserves HID's game-relative
// reference, not magnetic north or an independently calibrated Android sensor.
inline std::optional<MotionQuaternion> AndroidOrientationFromHid(const MotionMatrix& columns) {
    constexpr float tolerance=0.03f;
    for(unsigned a=0;a<3;++a) {
        for(float x:columns[a]) if(!std::isfinite(x) || std::abs(x)>1.05f) return {};
        for(unsigned b=a;b<3;++b) {
            float dot=0;
            for(unsigned i=0;i<3;++i) dot+=columns[a][i]*columns[b][i];
            if(std::abs(dot-(a==b?1.0f:0.0f))>tolerance) return {};
        }
    }
    const auto& a=columns[0];const auto& b=columns[1];const auto& c=columns[2];
    const float determinant=a[0]*(b[1]*c[2]-b[2]*c[1])-
        b[0]*(a[1]*c[2]-a[2]*c[1])+c[0]*(a[1]*b[2]-a[2]*b[1]);
    if(std::abs(determinant-1.0f)>0.05f) return {}; // Reject reflections too.
    constexpr unsigned axis[3]{2,0,1};
    constexpr float sign[3]{1,-1,-1};
    MotionMatrix m{}; // Conventional row-major matrix, AFTER the HID boundary.
    for(unsigned r=0;r<3;++r) for(unsigned col=0;col<3;++col)
        m[r][col]=sign[r]*sign[col]*columns[axis[col]][axis[r]];

    MotionQuaternion q;
    const float trace=m[0][0]+m[1][1]+m[2][2];
    if(trace>0) {
        const float s=2*std::sqrt(std::max(0.0f,trace+1));
        if(s<0.00001f) return {};
        q={s/4,(m[2][1]-m[1][2])/s,(m[0][2]-m[2][0])/s,(m[1][0]-m[0][1])/s};
    } else if(m[0][0]>=m[1][1] && m[0][0]>=m[2][2]) {
        const float s=2*std::sqrt(std::max(0.0f,1+m[0][0]-m[1][1]-m[2][2]));
        if(s<0.00001f) return {};
        q={(m[2][1]-m[1][2])/s,s/4,(m[0][1]+m[1][0])/s,(m[0][2]+m[2][0])/s};
    } else if(m[1][1]>=m[2][2]) {
        const float s=2*std::sqrt(std::max(0.0f,1+m[1][1]-m[0][0]-m[2][2]));
        if(s<0.00001f) return {};
        q={(m[0][2]-m[2][0])/s,(m[0][1]+m[1][0])/s,s/4,(m[1][2]+m[2][1])/s};
    } else {
        const float s=2*std::sqrt(std::max(0.0f,1+m[2][2]-m[0][0]-m[1][1]));
        if(s<0.00001f) return {};
        q={(m[1][0]-m[0][1])/s,(m[0][2]+m[2][0])/s,(m[1][2]+m[2][1])/s,s/4};
    }
    const float norm=std::sqrt(QuaternionDot(q,q));
    if(!std::isfinite(norm) || norm<0.5f || norm>1.5f) return {};
    q={q.w/norm,q.x/norm,q.y/norm,q.z/norm};
    if(!UnitQuaternion(q)) return {};
    return q.w<0 ? NegateQuaternion(q):q;
}
} // namespace cloudgenshin::input
