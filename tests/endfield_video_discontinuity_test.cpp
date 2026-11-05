#include "endfield/video_discontinuity.hpp"
#include <cassert>
#include <string>
int main() {
    cloudgenshin::endfield::VideoDiscontinuity tracker;
    std::string calls;
    const auto loss=[&](unsigned count) {tracker.observe(count,[&]{calls+='R';});};
    loss(0);assert(calls.empty());
    loss(1);calls+='I'; // First recovered IDR must follow reset.
    loss(1);loss(1);assert(calls=="RI"); // A later timer cannot delete that IDR.
    loss(3);calls+='I';loss(3);assert(calls=="RIRI");
    loss(0);assert(calls=="RIRIR"); // Unsigned counter rollover.
}
