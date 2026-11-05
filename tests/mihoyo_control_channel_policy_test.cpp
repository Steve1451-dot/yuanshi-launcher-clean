#include "mihoyo/control_channel_policy.hpp"
#include <cassert>
int main() {
    using Policy=cloudgenshin::rtc::ControlChannelPolicy;using State=Policy::State;
    Policy policy;assert(!policy.ready());
    policy.begin(0);assert(policy.update(14999,false,false,false)==State::WaitingAssociation);
    assert(policy.update(15000,false,false,false)==State::AssociationTimeout);
    policy.begin(0);assert(policy.update(1,true,false,false)==State::InitFailed);
    policy.begin(0);assert(policy.update(100,false,true,false)==State::WaitingChannel);
    assert(policy.update(15099,false,true,false)==State::WaitingChannel);
    assert(policy.update(15100,false,true,false)==State::ChannelTimeout);
    policy.begin(0);assert(policy.update(1,false,true,true)==State::Ready && policy.ready());
    assert(policy.update(2,false,false,true)==State::Recovering && !policy.ready());
    assert(policy.update(5001,false,false,true)==State::Recovering);
    assert(policy.update(5001,false,true,true)==State::Ready);
    // A later outage gets its own deadline; repeated samples cannot extend it.
    assert(policy.update(6000,false,true,false)==State::Recovering);
    assert(policy.update(10999,false,false,false)==State::Recovering);
    assert(policy.update(11000,false,false,false)==State::Lost && policy.failed());
    assert(policy.update(11001,false,true,true)==State::Lost);
    policy.begin(0);assert(policy.update(1,false,true,true)==State::Ready);
    assert(policy.update(2,false,false,true)==State::Recovering);
    assert(policy.update(5002,false,true,true)==State::Lost);
    policy.reset();assert(policy.update(999999,false,true,true)==State::Inactive);
    policy.begin(0);assert(policy.update(1,false,false,true)==State::WaitingAssociation);
    assert(policy.update(2,false,true,true)==State::Ready);
}
