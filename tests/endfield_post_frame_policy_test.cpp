#include "endfield/post_frame_policy.hpp"
#include <cassert>
using cloudgenshin::endfield::PostFramePolicy;
int main() {
    using Write=PostFramePolicy::Write;
    PostFramePolicy policy;
    assert(!policy.has_frame());
    policy.intercept_accepted();
    assert(!policy.pipeline_ready());
    assert(!policy.try_write(Write::Fps,0) && !policy.try_write(Write::Intercept,0));
    policy.frame_received();
    assert(policy.has_frame());
    assert(!policy.pipeline_ready()); // A decoded frame alone is insufficient.
    for(int i=0;i<20;++i) assert(policy.try_write(Write::Fps,i*500));
    assert(!policy.try_write(Write::Fps,100000));
    assert(policy.try_write(Write::Intercept,0)); // Exhausting FPS cannot starve SDK startup.
    assert(!policy.pipeline_ready()); // Local write rejection must not open the gate.
    assert(!policy.try_write(Write::Intercept,499));
    assert(policy.try_write(Write::Intercept,500));
    policy.intercept_accepted();
    assert(policy.pipeline_ready());
    policy.frame_received();assert(policy.pipeline_ready());
    policy=PostFramePolicy{};
    assert(!policy.has_frame());
    assert(!policy.pipeline_ready());
    policy.frame_received();
    assert(policy.try_write(Write::Intercept,0) && policy.try_write(Write::Fps,0));
    for(int i=1;i<20;++i) assert(policy.try_write(Write::Intercept,i*500));
    assert(!policy.try_write(Write::Intercept,100000));
}
