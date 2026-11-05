#pragma once
#include <array>
#include <cstdint>
#include <cstddef>
namespace cloudgenshin::endfield {
// Bounded diagnostic-only rate limit; never delays/filters protocol handling.
class DiagnosticEventPolicy {
public:
    bool admit(std::uint32_t hash,std::int64_t now_ms) {
        if (now_ms<next_) return false;
        for (const auto& slot:slots_)
            if (slot.used && slot.hash==hash && now_ms<slot.next) return false;
        slots_[cursor_++%slots_.size()]={hash,now_ms+5000,true};
        next_=now_ms+250;
        return true;
    }
private:
    struct Slot {std::uint32_t hash=0;std::int64_t next=0;bool used=false;};
    std::array<Slot,16> slots_{};
    std::size_t cursor_=0;
    std::int64_t next_=0;
};
}
