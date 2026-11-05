#include "endfield/exit_drain.hpp"
#include "endfield/queue_cancel_policy.hpp"
#include <cassert>
#include <cstdint>
#include <string_view>

int main() {
    using namespace cloudgenshin::endfield;
    std::int64_t clock=0;
    unsigned writes=0,pumps=0;
    auto run=[&](auto connected,auto send) {
        return DrainExitCommand(connected,send,[&]{++pumps;},
                                [&]{return clock;},[&]{clock+=2;});
    };
    auto accepted=run([]{return true;},[&]{++writes;return true;});
    assert(accepted.accepted_locally && accepted.attempts==1 && writes==1);
    assert(pumps>0 && accepted.elapsed_ms==500); // No duplicate exit after enqueue.

    clock=0;writes=0;pumps=0;
    auto retried=run([]{return true;},[&]{return ++writes==3;});
    assert(retried.accepted_locally && retried.attempts==3 && clock==552);
    assert(pumps>0); // Rejected writes still service the transport.

    clock=0;writes=0;
    auto rejected=run([]{return true;},[&]{++writes;return false;});
    assert(!rejected.accepted_locally && rejected.attempts==10 && clock==250);

    clock=0;writes=0;pumps=0;
    auto closed=run([]{return false;},[&]{++writes;return true;});
    assert(!closed.accepted_locally && writes==0 && pumps==0 && clock==0);

    clock=0;writes=0;
    auto remote_closed=run([&]{return clock<100;},[&]{++writes;return true;});
    assert(remote_closed.accepted_locally && clock==100 && writes==1);

    clock=0;
    auto busy=DrainExitCommand([]{return true;},[]{return true;},[&]{clock+=2;},
                              [&]{return clock;},[&]{clock+=2;});
    assert(busy.elapsed_ms==500 && busy.attempts==1);
    assert(std::string_view(QueueCancelEvent(0))=="ENDFIELD_QUEUE_CANCELLED");
    assert(std::string_view(QueueCancelEvent(23001))=="ENDFIELD_QUEUE_NOT_ACTIVE");
    assert(std::string_view(QueueCancelEvent(22002))=="ENDFIELD_QUEUE_CANCEL_FAILED");
    assert(std::string_view(QueueCancelEvent(-1006))=="ENDFIELD_QUEUE_CANCEL_FAILED");
}
