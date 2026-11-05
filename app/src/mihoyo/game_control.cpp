#include "mihoyo/game_control.hpp"
#include <bit>
#include <limits>

namespace cloudgenshin::control {
namespace {
constexpr std::size_t kMaxBody = 64 * 1024;
bool SafeText(const std::string& value) {
    if (value.empty() || value.size() > 8192) return false;
    for (unsigned char c : value) if (c < 0x20 || c == 0x7f) return false;
    return true;
}
void Varint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    do {
        const auto byte = static_cast<std::uint8_t>(value & 127);
        value >>= 7;
        out.push_back(byte | (value ? 128 : 0));
    } while (value);
}
void Text(std::vector<std::uint8_t>& out, unsigned field, const std::string& value) {
    Varint(out, (field << 3) | 2); Varint(out, value.size());
    out.insert(out.end(), value.begin(), value.end());
}
class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    bool done() const { return offset_ == bytes_.size(); }
    bool varint(std::uint64_t& value) {
        value = 0;
        for (unsigned i = 0; i < 10; ++i) {
            if (done()) return false;
            const auto b = bytes_[offset_++];
            if (i == 9 && b > 1) return false;
            value |= static_cast<std::uint64_t>(b & 127) << (i * 7);
            if (!(b & 128)) return true;
        }
        return false;
    }
    bool bytes(std::span<const std::uint8_t>& view) {
        std::uint64_t size;
        if (!varint(size) || size > bytes_.size() - offset_) return false;
        view = bytes_.subspan(offset_, static_cast<std::size_t>(size));
        offset_ += view.size(); return true;
    }
    bool skip(unsigned wire) {
        std::uint64_t value = 0;
        std::span<const std::uint8_t> view;
        if (wire == 0) return varint(value);
        if (wire == 2) return bytes(view);
        const unsigned count = wire == 1 ? 8 : wire == 5 ? 4 : 0;
        if (!count || count > bytes_.size() - offset_) return false;
        offset_ += count; return true;
    }
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};
bool Config(std::span<const std::uint8_t> bytes, StartResult& result) {
    Reader reader(bytes);
    unsigned fields = 0;
    while (!reader.done()) {
        std::uint64_t key, value;
        if (++fields > 1024 || !reader.varint(key) || !(key >> 3) || key >> 3 > 0x1fffffff)
            return false;
        const unsigned field = static_cast<unsigned>(key >> 3), wire = key & 7;
        if (field == 1 || field == 2 || field == 8 || field == 9) {
            if (wire || !reader.varint(value) || value > 0x7fffffff) return false;
            if (field == 1) result.xbox_enabled = value == 1;
            else if (field == 2) result.ps_enabled = value == 1;
            else if (field == 9) result.gr0006_enabled = value == 1;
            else result.control_channel_type = static_cast<std::int32_t>(value);
        } else if (!reader.skip(wire)) return false;
    }
    return true;
}
} // namespace

std::optional<bool> DecodeImuFeedback(std::span<const std::uint8_t> body) {
    if(body.size()>4096) return std::nullopt;
    Reader reader(body);
    std::uint64_t type=0;
    bool have_type=false,have_feedback=false,enabled=false;
    unsigned fields=0;
    while(!reader.done()) {
        std::uint64_t key,value;
        if(++fields>128 || !reader.varint(key) || !(key>>3) || key>>3>0x1fffffff) return std::nullopt;
        const unsigned field=key>>3,wire=key&7;
        if(field==1) {
            if(have_type || wire || !reader.varint(type)) return std::nullopt;
            have_type=true;
        } else if(field==15) {
            std::span<const std::uint8_t> nested;
            if(have_feedback || wire!=2 || !reader.bytes(nested)) return std::nullopt;
            have_feedback=true;
            Reader feedback(nested);
            bool have_state=false;
            while(!feedback.done()) {
                if(++fields>128 || !feedback.varint(key) || !(key>>3) || key>>3>0x1fffffff) return std::nullopt;
                if(key>>3==1) {
                    if(have_state || (key&7) || !feedback.varint(value) || value>1) return std::nullopt;
                    have_state=true;enabled=value==1;
                } else if(!feedback.skip(key&7)) return std::nullopt;
            }
        } else if(!reader.skip(wire)) return std::nullopt;
    }
    if(!have_type || type!=23 || !have_feedback) return std::nullopt;
    return enabled;
}

std::vector<std::uint8_t> EncodePacket(const Packet& packet) {
    if (!packet.command || packet.header.size() > 4096 || packet.body.size() > kMaxBody)
        return {};
    std::vector<std::uint8_t> out{0x45,0x67};
    const auto append = [&out](std::uint32_t value, unsigned bytes) {
        for (unsigned i = bytes; i; --i) out.push_back(static_cast<std::uint8_t>(value >> ((i-1)*8)));
    };
    append(packet.command, 2); append(static_cast<std::uint32_t>(packet.header.size()), 2);
    append(static_cast<std::uint32_t>(packet.body.size()), 4);
    out.insert(out.end(), packet.header.begin(), packet.header.end());
    out.insert(out.end(), packet.body.begin(), packet.body.end());
    out.insert(out.end(), {0x89,0xab}); return out;
}

std::optional<Packet> DecodePacket(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < 12 || bytes[0] != 0x45 || bytes[1] != 0x67 ||
        bytes[bytes.size()-2] != 0x89 || bytes.back() != 0xab) return std::nullopt;
    const std::size_t head = (bytes[4] << 8) | bytes[5];
    const std::uint32_t body = (static_cast<std::uint32_t>(bytes[6]) << 24) |
        (static_cast<std::uint32_t>(bytes[7]) << 16) | (bytes[8] << 8) | bytes[9];
    if (head > 4096 || body > kMaxBody || bytes.size() != 12 + head + body) return std::nullopt;
    Packet result;
    result.command = static_cast<std::uint16_t>((bytes[2] << 8) | bytes[3]);
    if (!result.command) return std::nullopt;
    result.header.assign(bytes.begin()+10, bytes.begin()+10+head);
    result.body.assign(bytes.begin()+10+head, bytes.end()-2); return result;
}

std::vector<std::uint8_t> EncodeStartGame(const StartGame& request) {
    for (const auto* field : {&request.game_token, &request.account_id, &request.sdk_version,
                              &request.sid, &request.pod_id})
        if (!SafeText(*field)) return {};
    if (!request.terminal_type || request.terminal_type > 700) return {};
    Packet packet{kStartGameReq, {}, {}};
    Text(packet.body, 1, request.game_token); Text(packet.body, 2, request.account_id);
    Text(packet.body, 3, request.sdk_version);
    Varint(packet.body, 4 << 3); Varint(packet.body, request.terminal_type);
    Text(packet.body, 6, request.sid); Text(packet.body, 7, request.pod_id);
    // Field 8 is has_rtc_connected; the official client sends StartGameReq
    // before the RTC connection exists, so it stays zero and is omitted.
    if (request.has_rtc_connected) {
        Varint(packet.body, 8 << 3);
        Varint(packet.body, 1);
    }
    if (request.since_start_sec) {
        Varint(packet.body, 9 << 3);
        Varint(packet.body, request.since_start_sec);
    }
    if (request.link_tasks_ms) {
        Varint(packet.body, 10 << 3);
        Varint(packet.body, request.link_tasks_ms);
    }
    // Field 12: common.SdkGameConfig. The APK (yscloud 7.1.0) shows this
    // submessage has no gamepad field at all, so it stays empty exactly like
    // the official clients. Controller capability is not declarable here.
    packet.body.insert(packet.body.end(), {0x62, 0x00});
    return EncodePacket(packet);
}

std::vector<std::uint8_t> EncodeStopGame(const std::string& game_token) {
    if (!SafeText(game_token)) return {};
    Packet packet{kStopGameReq, {}, {}};
    Text(packet.body, 1, game_token); return EncodePacket(packet);
}

std::vector<std::uint8_t> EncodeResetInputs(const std::string& game_token) {
    if (!SafeText(game_token)) return {};
    Packet packet{kResetInputsReq, {}, {}};
    Text(packet.body, 1, game_token); return EncodePacket(packet);
}

std::vector<std::uint8_t> EncodeHeartbeat(std::uint64_t id, std::int64_t unix_ms) {
    if (!id || id > 0x7fffffffffffffffULL || unix_ms <= 0) return {};
    Packet packet{20029, {}, {}};
    // proto.msg.Heartbeat: field 1 heartbeat_id, field 2 send_timestamp, both
    // int64. The pod echoes the pair so the client can measure the round trip;
    // the native Android SDK descriptor uses the same two field numbers.
    Varint(packet.body, 1 << 3); Varint(packet.body, id);
    Varint(packet.body, 2 << 3); Varint(packet.body, static_cast<std::uint64_t>(unix_ms));
    return EncodePacket(packet);
}

std::vector<std::uint8_t> EncodeSetFps(std::uint32_t fps) {
    if (!fps || fps > 240) return {};
    Packet packet{kSetFpsReq, {}, {}};
    Varint(packet.body, 1 << 3); Varint(packet.body, fps);
    return EncodePacket(packet);
}

std::vector<std::uint8_t> EncodeKeyframeRequest() {
    return EncodePacket({kKeyFrameReq,{}, {}});
}

std::vector<std::uint8_t> EncodeSdkDeviceInfo(const DeviceInfo& info) {
    if (!SafeText(info.game_token) || !SafeText(info.sdk_version)) return {};
    if (!info.terminal_type || info.terminal_type > 700) return {};
    if (info.screen_width > 0xffff || info.screen_height > 0xffff) return {};
    for (const auto* field : {&info.device_id, &info.device_name, &info.device_gpu,
                              &info.device_system, &info.network_type, &info.server_ip,
                              &info.transport_protocol})
        if (!field->empty() && !SafeText(*field)) return {};
    Packet packet{kSdkDeviceInfo, {}, {}};
    // Field order follows the official serializer (ascending field number).
    Text(packet.body, 1, info.game_token);
    Text(packet.body, 2, info.sdk_version);
    Varint(packet.body, 3 << 3); Varint(packet.body, info.terminal_type);
    if (!info.device_id.empty()) Text(packet.body, 4, info.device_id);
    if (!info.device_name.empty()) Text(packet.body, 6, info.device_name);
    if (!info.device_gpu.empty()) Text(packet.body, 8, info.device_gpu);
    if (!info.device_system.empty()) Text(packet.body, 9, info.device_system);
    if (info.screen_width) {Varint(packet.body, 11 << 3); Varint(packet.body, info.screen_width);}
    if (info.screen_height) {Varint(packet.body, 12 << 3); Varint(packet.body, info.screen_height);}
    if (!info.network_type.empty()) Text(packet.body, 15, info.network_type);
    if (!info.server_ip.empty()) Text(packet.body, 17, info.server_ip);
    if (!info.transport_protocol.empty()) Text(packet.body, 20, info.transport_protocol);
    return EncodePacket(packet);
}

std::vector<std::uint8_t> EncodeSetGraphicsMode(std::int32_t mode) {
    if (mode < kGraphicsHighQuality || mode > kGraphicsExtremeSaving) return {};
    Packet packet{kSetGraphicsModeReq, {}, {}};
    // Proto3 omits the default: mode 0 (high quality) is an empty body.
    if (mode != 0) { Varint(packet.body, 1 << 3); Varint(packet.body, static_cast<std::uint64_t>(mode)); }
    return EncodePacket(packet);
}

std::optional<std::int32_t> DecodeSyncGameControlMode(std::span<const std::uint8_t> body) {
    if (body.size() > kMaxBody) return std::nullopt;
    Reader reader(body);
    std::int32_t mode = 0;
    unsigned fields = 0;
    while (!reader.done()) {
        std::uint64_t key, value;
        if (++fields > 64 || !reader.varint(key) || !(key >> 3) || key >> 3 > 0x1fffffff)
            return std::nullopt;
        const unsigned field = static_cast<unsigned>(key >> 3), wire = key & 7;
        if (field == 1) {
            if (wire || !reader.varint(value) || value > 0x7fffffff) return std::nullopt;
            mode = static_cast<std::int32_t>(value);
        } else if (!reader.skip(wire)) return std::nullopt;
    }
    return mode;
}

std::optional<StartResult> DecodeStartResult(std::span<const std::uint8_t> body) {
    if (body.size() > kMaxBody) return std::nullopt;
    Reader reader(body);
    StartResult result;
    unsigned fields = 0;
    while (!reader.done()) {
        std::uint64_t key, value;
        if (++fields > 1024 || !reader.varint(key) || !(key >> 3) || key >> 3 > 0x1fffffff)
            return std::nullopt;
        const unsigned field = static_cast<unsigned>(key >> 3), wire = key & 7;
        if (field == 1 || field == 2) {
            if (wire || !reader.varint(value)) return std::nullopt;
            if (field == 2) {
                if (value > std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
                result.game_app_id = static_cast<std::uint32_t>(value);
            } else {
                const auto signed_value = std::bit_cast<std::int64_t>(value);
                if (signed_value < std::numeric_limits<std::int32_t>::min() ||
                    signed_value > std::numeric_limits<std::int32_t>::max()) return std::nullopt;
                result.retcode = static_cast<std::int32_t>(signed_value);
            }
        } else if (field == 3) {
            std::span<const std::uint8_t> config;
            if (wire != 2 || !reader.bytes(config) || !Config(config, result)) return std::nullopt;
            result.has_config = true;
        } else if (!reader.skip(wire)) return std::nullopt;
    }
    return result;
}
} // namespace cloudgenshin::control
