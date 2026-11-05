#include "mihoyo/safe_log_writer.hpp"
#include <atomic>
#include <cassert>
#include <future>
#include <stdexcept>

int main() {
    using namespace std::chrono_literals;
    std::atomic_int calls=0;
    std::promise<void> started,release;
    auto gate=release.get_future().share();
    {
        cloudgenshin::SafeLogWriter writer([&]{
            if(++calls==1) {started.set_value();gate.wait();}
        });
        assert(writer.request());
        assert(started.get_future().wait_for(2s)==std::future_status::ready);
        // Simulate a stalled SD write. Requests still return immediately and
        // occupy one dirty bit, never a growing queue of 10000 snapshots.
        for(int i=0;i<10000;++i) assert(writer.request());
        assert(calls==1);
        release.set_value();
    }
    assert(calls==2); // Final pending state is flushed even on destruction.
    std::promise<void> failed;
    calls=0;
    {
        cloudgenshin::SafeLogWriter writer([&]{
            if(++calls==1) {failed.set_value();throw std::runtime_error("synthetic write failure");}
        });
        assert(writer.request());
        assert(failed.get_future().wait_for(2s)==std::future_status::ready);
        assert(writer.request());
    }
    assert(calls==2); // A failing save does not kill the writer or caller.
}
