#include "mihoyo/setting_delivery_policy.hpp"
#include "mihoyo/stream_quality_policy.hpp"
#include "endfield/adaptive_bitrate.hpp"
#include <cassert>

int main() {
    using namespace cloudgenshin;
    using namespace cloudgenshin::overlay;
    SettingRequest request;SettingSendBudget budget;
    request.retry();assert(request.snapshot().state()==SettingRequest::State::Unset);
    request.request(30);auto revision=*request.pending();
    assert(budget.try_write(revision,0));budget.sent(false);
    assert(!budget.try_write(revision,499));
    for(int now=500;now<=1500;now+=500) {
        assert(budget.try_write(revision,now));budget.sent(true);
        assert(budget.delivered()==(now==1500));
    }
    request.complete(revision,budget.delivered());
    assert(request.snapshot().state()==SettingRequest::State::Sent);
    assert(!budget.try_write(revision,100000)); // No steady-state command flood.
    request.retry();revision=*request.pending();
    assert(revision.value()==30 && budget.try_write(revision,100001));
    budget.sent(false);
    // A user choice made during a recovery cannot be overwritten by an old send.
    request.request(60);request.complete(revision,true);
    assert(request.pending()->value()==60);
    revision=*request.pending();
    for(int attempt=0;attempt<20;++attempt) {
        assert(budget.try_write(revision,101000+attempt*500));budget.sent(false);
    }
    assert(budget.exhausted() && !budget.delivered());
    request.complete(revision,false);request.retry();
    revision=*request.pending();assert(revision.value()==60 && budget.try_write(revision,111000));
    // Recovery refreshes a still-pending budget as well as sent/failed requests.
    const auto before=revision.bits;request.retry();revision=*request.pending();
    assert(before!=revision.bits && budget.try_write(revision,111001));

    assert(!StreamQualityBitrate(-1) && !StreamQualityBitrate(3));
    for(int tier=0;tier<3;++tier) {
        const auto range=*StreamQualityBitrate(tier);
        assert(range.minimum>=3000 && range.maximum>=range.minimum);
        assert(MihoyoBitrate(range.maximum)==static_cast<unsigned>(range.maximum)*1024);
        endfield::AdaptiveBitrate adaptive;
        assert(adaptive.configure(range.minimum,range.maximum,0));
        endfield::AdaptiveBitrate::Sample damage{};
        adaptive.observe(0,damage);
        for(int now=5000;now<=200000;now+=5000) {
            damage.completed+=280;damage.damaged+=20;damage.timeouts+=25;
            if(const auto cap=adaptive.observe(now,damage)) {
                assert(*cap>=range.minimum && *cap<=range.maximum);
                adaptive.sent(now,true);
            }
        }
        assert(adaptive.current()==range.minimum);
    }
}
