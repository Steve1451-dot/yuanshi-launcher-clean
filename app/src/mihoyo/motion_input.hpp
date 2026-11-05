#pragma once
#include "mihoyo/motion_orientation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace cloudgenshin::input {
using MotionVector = std::array<float,3>;
struct MotionSample {
    MotionVector gyro{};         // Android device coordinates, radians/second.
    MotionVector acceleration{}; // Android device coordinates, m/s^2.
    MotionVector gravity{};      // Android gravity direction, unit length.
    std::optional<MotionQuaternion> orientation; // Absent for invalid/missing HID matrices.
    std::uint64_t sequence=0;
    std::int64_t sampled_ms=0;
    int source=-1;
    bool valid=false;
};
inline bool FiniteMotion(const MotionVector& v) {
    for(float x:v) if(!std::isfinite(x)) return false;
    return true;
}
inline float MotionLength(const MotionVector& v) {
    return std::sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
}

// Same landscape basis as Borealis/Moonlight, then inverse ROTATION_90 to
// Android's *natural portrait* sensor coordinates. Android sensors do not
// rotate with the display. Accelerometer and angular velocity have opposite
// native HID signs. Keep gyro in rad/s, NOT Moonlight's wire unit (deg/s).
class MotionTracker {
public:
    MotionSample update(int source,std::uint64_t number,std::int64_t now,
                        const MotionVector& accel_g,const MotionVector& gyro_turns,
                        const MotionMatrix* direction=nullptr) {
        if(source!=source_) {reset();source_=source;}
        if(source<0 || !number || !FiniteMotion(accel_g) || !FiniteMotion(gyro_turns)) {
            reset();source_=source;return {};
        }
        if(number==number_) return sample_; // Never refresh a frozen sensor's age.
        if(sample_.valid && (now<sample_.sampled_ms || now-sample_.sampled_ms>100 || number<number_)) reset();
        constexpr float g=9.80665f,tau=0.12f,two_pi=6.283185307179586f;
        const MotionVector a{-accel_g[2],accel_g[0],accel_g[1]};
        const float length=MotionLength(a);
        if(!std::isfinite(length) || length>16.0f) {reset();source_=source;return {};}
        for(float x:gyro_turns) if(std::abs(x)>32.0f) {reset();source_=source;return {};}
        // TYPE_GRAVITY is not exposed by HID. Estimate only from credible
        // acceleration, never normalise zero/free-fall or a large impact.
        // Gravity fallback stays separate from the HID orientation; do not
        // silently change Endfield's gravity behaviour with this pose update.
        if(length>=0.75f && length<=1.25f) {
            const float dt=sample_.valid ? std::clamp((now-sample_.sampled_ms)/1000.0f,0.0f,0.1f):0;
            const float alpha=sample_.valid ? dt/(tau+dt):1.0f;
            for(unsigned i=0;i<3;++i) gravity_[i]+=alpha*(a[i]/length-gravity_[i]);
        }
        const float gravity_length=MotionLength(gravity_);
        if(gravity_length<0.1f || !std::isfinite(gravity_length)) return {};
        MotionSample out;
        if(direction) out.orientation=AndroidOrientationFromHid(*direction);
        if(out.orientation && previous_orientation_ &&
           QuaternionDot(*out.orientation,*previous_orientation_)<0)
            out.orientation=NegateQuaternion(*out.orientation);
        // Never carry the last valid pose into a sample with a bad matrix.
        previous_orientation_=out.orientation;
        out.gyro={gyro_turns[2]*two_pi,-gyro_turns[0]*two_pi,-gyro_turns[1]*two_pi};
        for(unsigned i=0;i<3;++i) {
            out.acceleration[i]=a[i]*g;
            out.gravity[i]=gravity_[i]/gravity_length;
        }
        out.sequence=++sequence_;out.sampled_ms=now;out.source=source;out.valid=true;
        number_=number;sample_=out;return out;
    }
    void reset() {number_=0;sample_={};gravity_={};previous_orientation_.reset();}
private:
    int source_=-1;
    std::uint64_t number_=0,sequence_=0;
    MotionVector gravity_{};
    MotionSample sample_{};
    std::optional<MotionQuaternion> previous_orientation_;
};

// Worker-owned latest-only policy: no queue, no stale retry, no sensor activity
// masquerading as a keep-playing action. Transport failures are rate limited.
class MotionSendPolicy {
public:
    void enable(bool enabled) {
        if(enabled!=enabled_) {last_sequence_=0;next_ms_=0;last_gravity_={};have_gravity_=false;}
        enabled_=enabled;
    }
    bool enabled() const {return enabled_;}
    bool admit(const MotionSample& s,std::int64_t now,int interval_ms,bool changed_only=false) {
        if(!enabled_ || !s.valid || s.source<0 || !s.sequence ||
           now<s.sampled_ms || now-s.sampled_ms>100 || now<next_ms_ ||
           s.sequence==last_sequence_ || !FiniteMotion(s.gyro) ||
           !FiniteMotion(s.acceleration) || !FiniteMotion(s.gravity) ||
           (s.orientation && !UnitQuaternion(*s.orientation))) return false;
        const float length=MotionLength(s.gravity);
        if(!std::isfinite(length) || std::abs(length-1.0f)>0.02f) return false;
        if(changed_only && have_gravity_ && s.source==source_) {
            bool changed=false;
            for(unsigned i=0;i<3;++i) changed|=std::abs(s.gravity[i]-last_gravity_[i])>0.001f;
            if(!changed) return false;
        }
        last_sequence_=s.sequence;source_=s.source;
        next_ms_=now+std::max(16,interval_ms);return true;
    }
    void sent(const MotionSample& s) {last_gravity_=s.gravity;have_gravity_=true;++sent_;}
    void failed() {++failed_;}
    std::uint64_t sent_count() const {return sent_;}
    std::uint64_t failure_count() const {return failed_;}
private:
    bool enabled_=false,have_gravity_=false;
    int source_=-1;
    std::int64_t next_ms_=0;
    std::uint64_t last_sequence_=0,sent_=0,failed_=0;
    MotionVector last_gravity_{};
};
} // namespace cloudgenshin::input
