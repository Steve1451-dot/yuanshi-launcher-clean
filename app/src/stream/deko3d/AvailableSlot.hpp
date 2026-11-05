#pragma once
#include <cstddef>
#include <optional>
namespace opennow::video {
// A busy cursor is not a busy ring. Check each existing slot at most once,
// nonblocking; never wait for GPU work or grow the ring.
template<class Ready>
std::optional<std::size_t> AvailableSlot(std::size_t count,std::size_t cursor,Ready ready) {
    for(std::size_t i=0;i<count;++i) {
        const auto slot=(cursor+i)%count;
        if(ready(slot)) return slot;
    }
    return std::nullopt;
}
}
