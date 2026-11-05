#include "endfield/startup_auth_policy.hpp"
#include <cassert>
using cloudgenshin::endfield::StartupAuthPolicy;
int main() {
    using State=StartupAuthPolicy::State;
    StartupAuthPolicy auth;
    assert(auth.poll(999999)==State::Idle);
    auth.begin(1000);auth.begin(25000); // Duplicate protoVersion cannot extend it.
    assert(auth.poll(30999)==State::Waiting);
    auth.result("unexpected-private-body");
    assert(auth.poll(31000)==State::TimedOut);
    StartupAuthPolicy accepted;accepted.begin(0);accepted.result("success");
    assert(accepted.poll(90000)==State::Accepted);
    accepted.result("failure");assert(accepted.poll(90001)==State::Rejected);
    StartupAuthPolicy fast;fast.result("success");fast.begin(0);
    assert(fast.poll(90000)==State::Accepted);
    StartupAuthPolicy rejected;rejected.begin(0);rejected.result("failure");
    assert(rejected.poll(100)==State::Rejected);
}
