#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

namespace cloudgenshin::endfield {
// Switch-side ceiling control over the already verified WeLink range command.
// Not a claim to implement Android's GCC/FEC. Only transport damage drives it:
// UI/display skips, menu opening and decode congestion are deliberately absent.
class AdaptiveBitrate {
public:
    struct Sample {std::uint32_t completed=0,damaged=0,timeouts=0,capacity=0;};
    bool configure(int minimum,int maximum,std::int64_t now) {
        if(minimum<1000 || maximum<minimum || maximum>12000) return false;
        minimum_=minimum;maximum_=current_=maximum;
        have_sample_=false;pending_=0;bad_windows_=0;clean_ms_=0;next_change_=now+10000;
        return true;
    }
    std::optional<int> observe(std::int64_t now,Sample sample) {
        if(!current_) return std::nullopt;
        if(!have_sample_ || now<sample_ms_ || now-sample_ms_>15000) {
            have_sample_=true;previous_=sample;sample_ms_=now;clean_ms_=0;bad_windows_=0;
            return std::nullopt;
        }
        const auto elapsed=now-sample_ms_;
        if(elapsed<4000) return std::nullopt;
        const Sample delta{sample.completed-previous_.completed,sample.damaged-previous_.damaged,
            sample.timeouts-previous_.timeouts,sample.capacity-previous_.capacity};
        previous_=sample;sample_ms_=now;
        // A new SSRC/session resets cumulative counters. Do not interpret
        // unsigned subtraction across that reset as catastrophic packet loss.
        if(delta.completed>100000 || delta.damaged>100000 || delta.timeouts>100000 || delta.capacity>100000) {
            clean_ms_=0;bad_windows_=0;next_change_=now+10000;return std::nullopt;
        }
        const bool pressure=delta.capacity>0 || (delta.timeouts>=3 && delta.damaged>0) || delta.damaged>=3;
        if(pressure) {++bad_windows_;clean_ms_=0;}
        else {bad_windows_=0;clean_ms_=delta.completed>=30 ? clean_ms_+elapsed : 0;}
        if(pending_ || now<next_change_) return std::nullopt;
        if(bad_windows_>=2) pending_=std::max(minimum_,current_*3/4);
        else if(clean_ms_>=30000) pending_=std::min(maximum_,current_+std::max(250,current_/10));
        if(pending_==current_) pending_=0;
        return pending_ ? std::optional<int>{pending_} : std::nullopt;
    }
    void sent(std::int64_t now,bool accepted) {
        if(!pending_) return;
        if(accepted) current_=pending_;
        pending_=0;bad_windows_=0;clean_ms_=0;
        next_change_=now+(accepted ? 10000 : 5000);
    }
    int minimum() const {return minimum_;}
    int current() const {return current_;}
private:
    Sample previous_;
    int minimum_=0,maximum_=0,current_=0,pending_=0;
    bool have_sample_=false;
    unsigned bad_windows_=0;
    std::int64_t sample_ms_=0,next_change_=0,clean_ms_=0;
};
} // namespace cloudgenshin::endfield
