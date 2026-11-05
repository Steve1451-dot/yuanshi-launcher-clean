#pragma once
#include <cstdint>
namespace cloudgenshin::endfield {
// Owned by the packet loop. Observe before delivering the next complete AU,
// not in a delayed UI/diagnostic timer that could discard a newer good IDR.
class VideoDiscontinuity {
public:
    template<class Resync> void observe(std::uint32_t dropped, Resync&& resync) {
        if (dropped==last_) return;
        last_=dropped;
        resync();
    }
private:
    std::uint32_t last_=0;
};
}
