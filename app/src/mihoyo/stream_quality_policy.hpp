#pragma once
#include <array>
#include <cstdint>
#include <optional>

namespace cloudgenshin {
struct StreamBitrateRange {int minimum,maximum;}; // Decimal kbps in application policy.
// Keep a useful floor even in the lowest tier, while leaving room for recovery
// to lower the ceiling without building latency. Both services use these tiers.
inline constexpr std::array<StreamBitrateRange,3> kStreamQualityBitrates{{
    {6000,12000},{4000,8000},{3000,4000}}};
inline std::optional<StreamBitrateRange> StreamQualityBitrate(int tier) {
    if(tier<0 || tier>=static_cast<int>(kStreamQualityBitrates.size())) return std::nullopt;
    return kStreamQualityBitrates[tier];
}
// The official miHoYo web UI multiplies its kbps controls by 1024 for both
// dispatch.bit_rate and RtcSetBitrate. WeLink SimpleCmd instead uses plain kbps.
inline constexpr std::uint32_t MihoyoBitrate(int kbps) {
    return static_cast<std::uint32_t>(kbps)*1024;
}
} // namespace cloudgenshin
