#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace cloudgenshin::endfield {
// Web onFirstVideoFrame -> FIRST_VIDEO_FRAME_RECEIVED(6038) first enables
// app interception (R4), then gameManager sends PipelineConnected. PipeState
// is a different callback (6252). A setting retry must not starve this bridge.
class PostFramePolicy {
public:
    enum class Write : std::size_t { Intercept, Fps };
    void frame_received() {frame_=true;}
    bool has_frame() const {return frame_;}
    void intercept_accepted() {if(frame_) intercept_=true;}
    bool pipeline_ready() const {return frame_ && intercept_;}
    bool try_write(Write command,std::int64_t now) {
        if(!frame_) return false;
        auto& budget=budgets_[static_cast<std::size_t>(command)];
        if(budget.attempts>=20 || now<budget.next) return false;
        ++budget.attempts;
        budget.next=now+500;
        return true;
    }
private:
    struct Budget {unsigned attempts=0;std::int64_t next=0;};
    std::array<Budget,2> budgets_{};
    bool frame_=false,intercept_=false;
};
} // namespace cloudgenshin::endfield
