#include "mihoyo/sdk_start_game_params.hpp"

#include <bit>
#include <limits>
#include <utility>

namespace cloudgenshin::dispatch {
namespace {

constexpr std::size_t kMaximumBytes = 64 * 1024;
constexpr std::size_t kMaximumStringBytes = 8192;
constexpr std::size_t kMaximumAlternatives = 16;
constexpr std::size_t kMaximumFields = 1024;

// Protobuf strings are UTF-8; reject invalid sequences and delimiter/NUL
// injection before they can reach a native C-string endpoint consumer.
bool SafeUtf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        if (first < 0x80) {
            if (first < 0x20 || first == 0x7f) return false;
            continue;
        }
        unsigned count;
        std::uint32_t code;
        std::uint32_t minimum;
        if (first >= 0xc2 && first <= 0xdf) {
            count = 1; code = first & 0x1f; minimum = 0x80;
        } else if (first >= 0xe0 && first <= 0xef) {
            count = 2; code = first & 0x0f; minimum = 0x800;
        } else if (first >= 0xf0 && first <= 0xf4) {
            count = 3; code = first & 0x07; minimum = 0x10000;
        } else return false;
        if (count > text.size() - i) return false;
        for (unsigned j = 0; j < count; ++j) {
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

class WireReader {
public:
    explicit WireReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool empty() const { return offset_ == bytes_.size(); }

    bool Varint(std::uint64_t& value) {
        value = 0;
        for (unsigned i = 0; i < 10; ++i) {
            if (empty()) return false;
            const auto byte = bytes_[offset_++];
            if (i == 9 && byte > 1) return false;
            value |= static_cast<std::uint64_t>(byte & 0x7f) << (i * 7);
            if ((byte & 0x80) == 0) return true;
        }
        return false;
    }

    bool Text(std::string& text) {
        std::uint64_t size;
        if (!Varint(size) || size > kMaximumStringBytes ||
            size > bytes_.size() - offset_) return false;
        const std::string_view view(
            reinterpret_cast<const char*>(bytes_.data() + offset_),
            static_cast<std::size_t>(size));
        if (!SafeUtf8(view)) return false;
        text.assign(view);
        offset_ += static_cast<std::size_t>(size);
        return true;
    }

    bool Skip(unsigned wire) {
        std::uint64_t size;
        switch (wire) {
            case 0: return Varint(size);
            case 1: size = 8; break;
            case 2: if (!Varint(size)) return false; break;
            case 5: size = 4; break;
            // Groups are not emitted by this proto3 schema. Reject rather
            // than recurse through unbounded/unterminated unknown groups.
            default: return false;
        }
        if (size > bytes_.size() - offset_) return false;
        offset_ += static_cast<std::size_t>(size);
        return true;
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

int Base64Digit(unsigned char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

std::optional<SdkStartGameParams> ParseSdkStartGameParams(
    std::span<const std::uint8_t> protobuf) {
    if (protobuf.empty() || protobuf.size() > kMaximumBytes) return std::nullopt;
    WireReader reader(protobuf);
    SdkStartGameParams result;
    std::size_t fields = 0;
    while (!reader.empty()) {
        std::uint64_t key;
        if (++fields > kMaximumFields || !reader.Varint(key) ||
            key > std::numeric_limits<std::uint32_t>::max() || (key >> 3) == 0)
            return std::nullopt;
        const auto field = static_cast<unsigned>(key >> 3);
        const auto wire = static_cast<unsigned>(key & 7);
        std::string* text = nullptr;
        std::vector<std::string>* alternatives = nullptr;
        switch (field) {
            case 1: text = &result.sid; break;
            case 2: text = &result.ca_id; break;
            case 3: text = &result.pod_id; break;
            case 4: text = &result.game_token; break;
            case 5: text = &result.resolution; break;
            case 7: text = &result.cmd_line; break;
            case 8: text = &result.game_svr_addr; break;
            case 10: alternatives = &result.additional_game_svr_addrs; break;
            case 11: text = &result.game_server_wss_url; break;
            case 12: alternatives = &result.additional_game_server_wss_urls; break;
            case 14: text = &result.game_control_channel_url; break;
            case 15: alternatives = &result.additional_game_control_channel_urls; break;
            case 17: text = &result.game_svr_addr_ipv6; break;
            case 19: alternatives = &result.additional_game_svr_addrs_ipv6; break;
            case 20: text = &result.game_server_wss_url_ipv6; break;
            case 21: alternatives = &result.additional_game_server_wss_urls_ipv6; break;
            case 22: text = &result.game_control_channel_url_ipv6; break;
            case 23: alternatives = &result.additional_game_control_channel_urls_ipv6; break;
            default: break;
        }
        if (text || alternatives) {
            std::string value;
            if (wire != 2 || !reader.Text(value)) return std::nullopt;
            if (text) *text = std::move(value); // Protobuf singular: last wins.
            else {
                if (alternatives->size() >= kMaximumAlternatives) return std::nullopt;
                alternatives->push_back(std::move(value));
            }
        } else if (field == 6 || field == 9 || field == 13 ||
                   field == 16 || field == 18) {
            std::uint64_t value;
            if (wire != 0 || !reader.Varint(value)) return std::nullopt;
            if (field == 16) result.is_ipv6 = value != 0;
            else if (field == 6) {
                // Negative int32 can be sign-extended to a ten-byte varint.
                if (value > 0xffffffffULL && value < 0xffffffff80000000ULL)
                    return std::nullopt;
                result.target_fps = std::bit_cast<std::int32_t>(
                    static_cast<std::uint32_t>(value));
            } else {
                if (value > 0xffffffffULL) return std::nullopt;
                const auto number = static_cast<std::uint32_t>(value);
                if (field == 9) result.rtc_udp_port = number;
                else if (field == 13) result.big_isp = number;
                else result.rtc_udp_port_ipv6 = number;
            }
        } else if (!reader.Skip(wire)) return std::nullopt;
    }
    return result;
}

std::optional<std::vector<std::uint8_t>> DecodeSdkBase64(std::string_view base64) {
    constexpr std::size_t kMaximumEncoded = ((kMaximumBytes + 2) / 3) * 4;
    if (base64.empty() || base64.size() > kMaximumEncoded || base64.size() % 4 == 1)
        return std::nullopt;
    std::string padded(base64);
    std::size_t padding = 0;
    while (padding < padded.size() && padded[padded.size() - 1 - padding] == '=')
        ++padding;
    if (padding > 2 || (padding && padded.size() % 4 != 0)) return std::nullopt;
    for (std::size_t i = 0; i < padded.size() - padding; ++i)
        if (Base64Digit(static_cast<unsigned char>(padded[i])) < 0) return std::nullopt;
    while (padded.size() % 4) { padded += '='; ++padding; }
    if (padding && (Base64Digit(static_cast<unsigned char>(
            padded[padded.size() - padding - 1])) & (padding == 2 ? 15 : 3)))
        return std::nullopt;
    const std::size_t decoded = (padded.size() / 4) * 3 - padding;
    if (decoded == 0 || decoded > kMaximumBytes) return std::nullopt;
    std::vector<std::uint8_t> bytes;
    bytes.reserve(decoded);
    for (std::size_t i = 0; i < padded.size(); i += 4) {
        std::uint32_t group = 0;
        for (std::size_t j = 0; j < 4; ++j) {
            const auto digit = padded[i + j] == '=' ? 0 :
                Base64Digit(static_cast<unsigned char>(padded[i + j]));
            group = (group << 6) | static_cast<std::uint32_t>(digit);
        }
        bytes.push_back(static_cast<std::uint8_t>(group >> 16));
        if (bytes.size() < decoded) bytes.push_back(static_cast<std::uint8_t>(group >> 8));
        if (bytes.size() < decoded) bytes.push_back(static_cast<std::uint8_t>(group));
    }
    return bytes;
}

std::optional<SdkStartGameParams> DecodeSdkStartGameParams(std::string_view base64) {
    const auto bytes=DecodeSdkBase64(base64);
    return bytes ? ParseSdkStartGameParams(*bytes) : std::nullopt;
}

} // namespace cloudgenshin::dispatch
