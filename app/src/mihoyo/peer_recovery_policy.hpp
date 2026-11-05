#pragma once

namespace cloudgenshin::rtc {
// Packet-repair storage, not a video playout queue. In-order packets are still
// delivered immediately. 512 slots absorb a normal 720p burst; growth stops at
// 2048 so a long outage cannot turn into unbounded memory use.
//
// 60 ms is a Switch latency budget, not an APK constant: it allows the initial
// APK-style NACK (16 ms), the RTT-derived retry and one more attempt before the
// depacketizer gives up and reports immediate RTP damage. The official
// receiver may wait longer; copying its full jitter-buffer deadline would add
// noticeable input latency on this client.
inline constexpr unsigned kVideoRecoveryHoldMs = 60;
inline constexpr unsigned kVideoRecoveryInitialPackets = 512;
inline constexpr unsigned kVideoRecoveryMaximumPackets = 2048;
} // namespace cloudgenshin::rtc
