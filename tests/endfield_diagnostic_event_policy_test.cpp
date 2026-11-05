#include "endfield/diagnostic_event_policy.hpp"
#include <cassert>
int main() {
    cloudgenshin::endfield::DiagnosticEventPolicy policy;
    assert(policy.admit(42,0));
    for(int t=1;t<5000;++t) assert(!policy.admit(42,t));
    assert(policy.admit(42,5000));
    unsigned admitted=0;
    for(int t=5001;t<6000;++t) admitted+=policy.admit(t,t);
    assert(admitted<=4);
}
