#include "stream/deko3d/AvailableSlot.hpp"
#include <array>
#include <cassert>
int main() {
    using opennow::video::AvailableSlot;
    std::array<bool,4> busy={true,false,false,true};int checks=0;
    auto chosen=AvailableSlot(busy.size(),0,[&](size_t i) {++checks;return !busy[i];});
    assert(chosen && *chosen==1 && checks==2);
    chosen=AvailableSlot(busy.size(),3,[&](size_t i) {return !busy[i];});
    assert(chosen && *chosen==1);
    checks=0;assert(!AvailableSlot(8,7,[&](size_t) {++checks;return false;}));assert(checks==8);
    checks=0;assert(!AvailableSlot(0,0,[&](size_t) {++checks;return true;}));assert(checks==0);
}
