#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

namespace cloudgenshin {
inline int EffectiveStreamFps(int requested) {
    // Missing proto3 fields decode as zero, not a one-FPS stream request.
    return requested>0 ? std::clamp(requested,1,60) : 30;
}
class ExitHold {
public:
    // Fires once per hold. The latch is cleared as soon as the combo is seen
    // again after firing, so a key that is still held (as in the stream view,
    // where every draw just reads the current pad state) does not re-trigger
    // the exit -- and thus the activity pop -- on every single frame.
    bool update(bool plus, bool minus, std::int64_t now_ms) {
        if (!plus || !minus) {started_.reset();fired_=false;return false;}
        if (!started_) started_=now_ms;
        if (now_ms-*started_<800) return false;
        if (fired_) return false;
        fired_=true;
        return true;
    }
private:
    std::optional<std::int64_t> started_;
    bool fired_=false;
};
} // namespace cloudgenshin
