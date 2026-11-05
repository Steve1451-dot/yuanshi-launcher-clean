#include "endfield/startup_write_queue.hpp"
#include <cassert>

int main() {
    using cloudgenshin::endfield::StartupWriteQueue;
    using State=StartupWriteQueue::State;
    StartupWriteQueue queue;
    int accepted=0,attempts=0;
    const auto send=[&](const auto& packet) {
        ++attempts;
        if(packet[0]==2 && attempts<3) return false;
        assert(packet[0]==++accepted);return true;
    };
    queue.begin({{1},{2},{3},{4}},0);
    assert(queue.poll(0,send)==State::Pending && accepted==1 && attempts==2);
    assert(queue.poll(99,send)==State::Pending && attempts==2);
    assert(queue.poll(100,send)==State::Ready && accepted==4 && attempts==5);
    assert(queue.poll(999999,send)==State::Ready && attempts==5);
    queue.begin({{1}},0);int failed_attempts=0;
    const auto fail=[&](const auto&){++failed_attempts;return false;};
    assert(queue.poll(0,fail)==State::Pending && failed_attempts==1);
    assert(queue.poll(14999,fail)==State::Pending && failed_attempts==2);
    assert(queue.poll(15000,fail)==State::Failed && failed_attempts==2);
    assert(queue.poll(15001,send)==State::Failed);
    // Reusing the session starts a fresh, bounded sequence.
    accepted=0;attempts=100;
    queue.begin({{1}},20000);assert(queue.poll(20000,send)==State::Ready);
    queue.begin({},0);assert(queue.poll(0,send)==State::Failed);
    queue.begin({{1},{2},{3},{4},{5}},0);assert(queue.poll(0,send)==State::Failed);
}
