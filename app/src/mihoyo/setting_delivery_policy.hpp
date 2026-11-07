#pragma once
#include "mihoyo/stream_overlay_policy.hpp"

namespace cloudgenshin::overlay {
// Worker-owned, bounded budget per revision. A locally queued write can be
// ignored while the pod finishes startup. Repeat idempotent settings three
// times, 500ms apart; never continuously resend during normal gameplay.
class SettingSendBudget {
public:
    bool try_write(SettingRequest::Snapshot request,std::int64_t now) {
        if(request.bits!=revision_) {revision_=request.bits;attempts_=accepted_=0;next_=now;}
        if(exhausted() || delivered() || now<next_) return false;
        ++attempts_;next_=now+500;return true;
    }
    void sent(bool accepted) {if(accepted) ++accepted_;}
    bool delivered() const {return accepted_>=3;}
    bool exhausted() const {return attempts_>=20;}
private:
    std::uint64_t revision_=0;
    unsigned attempts_=0,accepted_=0;
    std::int64_t next_=0;
};
} // namespace cloudgenshin::overlay
