#include "stream/DecodeQueuePolicy.hpp"

#include <cassert>

static_assert(opennow::video::MaximumDecodeUnitBytes() == 2 * 1024 * 1024);
static_assert(opennow::video::MaximumQueuedAccessUnits(30) == 4);
static_assert(opennow::video::MaximumQueuedAccessUnits(60) == 8);
static_assert(opennow::video::MaximumQueuedAccessUnits(120) == 8);
static_assert(opennow::video::MaximumDecodeQueueDelayMs() == 67);
static_assert(opennow::video::MaximumRepairBurstAccessUnits(30) == 6);
static_assert(opennow::video::MaximumRepairBurstAccessUnits(60) == 8);
static_assert(opennow::video::MaximumRepairBurstAccessUnits(120) == 8);

int main()
{
    assert(opennow::video::MaximumQueuedAccessUnits(1) == 2);
    assert(opennow::video::MaximumQueuedAccessUnits(60) == 8);
    return 0;
}
