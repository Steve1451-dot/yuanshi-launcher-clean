#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace cloudgenshin::signaling {

// Framing observed in the public Cloud Genshin web SDK.  Payloads are opaque;
// account, SDP and ICE values must not be logged by this layer.
enum class MessageType : std::uint32_t {
    Signaling = 1,
    Proxy = 2,
    Handshake = 3,
    Keepalive = 4,
};

struct Frame {
    MessageType type;
    std::vector<std::uint8_t> payload;
};

// The underlying WebSocket client accepts at most 4 MiB per message, including
// this protocol's eight-byte header.
constexpr std::size_t kMaximumPayloadBytes = 4 * 1024 * 1024 - 8;

inline void AppendU32Be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

inline std::uint32_t ReadU32Be(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

inline std::vector<std::uint8_t> EncodeFrame(MessageType type,
                                              const std::vector<std::uint8_t>& payload) {
    const auto raw_type = static_cast<std::uint32_t>(type);
    if (raw_type < 1 || raw_type > 4 || payload.size() > kMaximumPayloadBytes) return {};
    std::vector<std::uint8_t> output;
    output.reserve(8 + payload.size());
    AppendU32Be(output, raw_type);
    AppendU32Be(output, static_cast<std::uint32_t>(payload.size()));
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}

inline std::optional<Frame> DecodeFrame(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 8) return std::nullopt;
    const auto raw_type = ReadU32Be(bytes, 0);
    const auto payload_length = ReadU32Be(bytes, 4);
    if (raw_type < 1 || raw_type > 4 || payload_length > kMaximumPayloadBytes ||
        bytes.size() - 8 != payload_length) return std::nullopt;
    return Frame{static_cast<MessageType>(raw_type),
                 std::vector<std::uint8_t>(bytes.begin() + 8, bytes.end())};
}

} // namespace cloudgenshin::signaling
