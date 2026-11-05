#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cloudgenshin::rtc {

enum class SignalType { Offer, Answer, Candidate };

struct SignalMessage {
    SignalType type;
    std::string sdp;
    std::string candidate;
    std::string sdp_mid;
    int sdp_mline_index = -1;
};

// Parse the UTF-8 JSON carried by a type-1 binary signaling frame. No payload
// is logged by this layer: SDP and candidates can expose network addresses.
std::optional<SignalMessage> ParseSignalMessage(const std::vector<std::uint8_t>& payload);

// The answer uses the official web SDK's {"type":"answer","sdp":...} shape.
std::optional<std::vector<std::uint8_t>> EncodeAnswer(const std::string& sdp);

// "candidates" is the web SDK's client-to-server trickle-ICE message type.
std::optional<std::vector<std::uint8_t>> EncodeCandidate(
    const std::string& candidate, const std::string& sdp_mid, int sdp_mline_index);

} // namespace cloudgenshin::rtc
