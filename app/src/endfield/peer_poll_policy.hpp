#pragma once
#include "webrtc/peer_poll_policy.hpp"

namespace cloudgenshin::endfield {
inline constexpr unsigned kVideoRecoveryHoldMs=120;
// APK packet-repair architecture with a Switch memory/latency ceiling. The
// Android 8192-slot maximum is not a mandate to reserve that much on Switch.
inline constexpr unsigned kVideoRecoveryInitialPackets=512;
inline constexpr unsigned kVideoRecoveryMaximumPackets=2048;
// Compatibility aliases: no Endfield constants or wire contracts change.
using opennow::webrtc::PeerDrainResult;
using opennow::webrtc::DrainPeerPacketsBudgeted;
using opennow::webrtc::DrainPeerPackets;
using opennow::webrtc::PollMediaAroundSignaling;
} // namespace cloudgenshin::endfield
