#include "mihoyo/input_packet.hpp"

#include "controller_layout.hpp"

#include <algorithm>
#include <bit>
#include <limits>

namespace cloudgenshin::input {
namespace {

constexpr std::uint32_t kDpadUp = 0x0001;
constexpr std::uint32_t kDpadDown = 0x0002;
constexpr std::uint32_t kDpadLeft = 0x0004;
constexpr std::uint32_t kDpadRight = 0x0008;
constexpr std::uint32_t kStart = 0x0010;
constexpr std::uint32_t kBack = 0x0020;
constexpr std::uint32_t kLeftThumb = 0x0040;
constexpr std::uint32_t kRightThumb = 0x0080;
constexpr std::uint32_t kLeftShoulder = 0x0100;
constexpr std::uint32_t kRightShoulder = 0x0200;

void AppendVarint(std::vector<std::uint8_t>& out, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0) byte |= 0x80;
        out.push_back(byte);
    } while (value != 0);
}

void AppendKey(std::vector<std::uint8_t>& out, std::uint32_t field,
               std::uint32_t wire_type) {
    AppendVarint(out, (static_cast<std::uint64_t>(field) << 3) | wire_type);
}

void AppendUInt32(std::vector<std::uint8_t>& out, std::uint32_t field,
                  std::uint32_t value) {
    if (value == 0) return;
    AppendKey(out, field, 0);
    AppendVarint(out, value);
}

void AppendInt32(std::vector<std::uint8_t>& out, std::uint32_t field,
                 std::int32_t value) {
    if (value == 0) return;
    AppendKey(out, field, 0);
    // Match jspb BinaryWriter.writeInt32: negative values sign-extend to 64 bits.
    AppendVarint(out, static_cast<std::uint64_t>(static_cast<std::int64_t>(value)));
}

void AppendInt64(std::vector<std::uint8_t>& out, std::uint32_t field,
                 std::int64_t value) {
    if (value == 0) return;
    AppendKey(out, field, 0);
    AppendVarint(out, static_cast<std::uint64_t>(value));
}

void AppendBytes(std::vector<std::uint8_t>& out, std::uint32_t field,
                 const std::vector<std::uint8_t>& bytes) {
    AppendKey(out, field, 2);
    AppendVarint(out, bytes.size());
    out.insert(out.end(), bytes.begin(), bytes.end());
}

// jspb BinaryWriter.writeDouble omits zero values and writes little-endian
// IEEE754 bits, matching proto.common.RtcMouse (cursorX/cursorY are double).
void AppendDouble(std::vector<std::uint8_t>& out, std::uint32_t field, double value) {
    if (value == 0.0) return;
    AppendKey(out, field, 1);
    const auto bits = std::bit_cast<std::uint64_t>(value);
    for (unsigned index = 0; index < 8; ++index)
        out.push_back(static_cast<std::uint8_t>(bits >> (index * 8)));
}

// proto.common.RtcTouchEvent.pointX/pointY are float (fixed32, wire type 5),
// confirmed both by the 7.1.0 descriptor and by the generated serializer in
// libnetio.so (fmov s0 / stur s0 / tag 0x1d). Writing them as double made the
// pod treat the field as unknown and every touch landed at (0,0).
void AppendFloat(std::vector<std::uint8_t>& out, std::uint32_t field, float value) {
    if (value == 0.0f) return;
    AppendKey(out, field, 5);
    const auto bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned index = 0; index < 4; ++index)
        out.push_back(static_cast<std::uint8_t>(bits >> (index * 8)));
}

// Repeated uint32 in the packed encoding jspb emits by default: one
// length-delimited field containing the varints.
void AppendPackedUInt32(std::vector<std::uint8_t>& out, std::uint32_t field,
                        std::initializer_list<std::uint32_t> values) {
    std::vector<std::uint8_t> packed;
    for (const auto value : values) AppendVarint(packed, value);
    AppendBytes(out, field, packed);
}

void AppendU16Be(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

void AppendU32Be(std::vector<std::uint8_t>& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

std::uint16_t ReadU16Be(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

std::uint32_t ReadU32Be(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::vector<std::uint8_t> EncodeGamepadKey(const GamepadState& state) {
    std::vector<std::uint8_t> out;
    out.reserve(64);
    AppendUInt32(out, 1, state.vendor_id);
    AppendUInt32(out, 2, state.product_id);
    AppendUInt32(out, 3, state.buttons);
    AppendUInt32(out, 4, state.left_trigger);
    AppendUInt32(out, 5, state.right_trigger);
    AppendInt32(out, 6, state.left_thumb_x);
    AppendInt32(out, 7, state.left_thumb_y);
    AppendInt32(out, 8, state.right_thumb_x);
    AppendInt32(out, 9, state.right_thumb_y);
    AppendInt64(out, 10, state.rtt_trace_send_time);
    return out;
}

} // namespace

GamepadState ToGamepadState(const SwitchControllerState& state,
                            std::int64_t monotonic_millis,
                            std::uint32_t vendor_id,
                            std::uint32_t product_id,
                            std::string_view controller_layout) {
    std::uint32_t buttons = 0;
    if (state.dpad_up) buttons |= kDpadUp;
    if (state.dpad_down) buttons |= kDpadDown;
    if (state.dpad_left) buttons |= kDpadLeft;
    if (state.dpad_right) buttons |= kDpadRight;
    if (state.plus) buttons |= kStart;
    if (state.minus) buttons |= kBack;
    if (state.stick_left) buttons |= kLeftThumb;
    if (state.stick_right) buttons |= kRightThumb;
    if (state.l) buttons |= kLeftShoulder;
    if (state.r) buttons |= kRightShoulder;
    buttons |= opennow::MapFaceButtons(
        controller_layout, state.a, state.b, state.x, state.y);

    return {vendor_id, product_id, buttons,
            state.zl ? 255U : 0U, state.zr ? 255U : 0U,
            std::clamp(state.left_x, -32767, 32767),
            std::clamp(state.left_y, -32767, 32767),
            std::clamp(state.right_x, -32767, 32767),
            std::clamp(state.right_y, -32767, 32767),
            std::max<std::int64_t>(0, monotonic_millis)};
}

std::vector<std::uint8_t> EncodeGamepadChannel(const GamepadState& state) {
    std::vector<std::uint8_t> out;
    out.reserve(80);
    AppendUInt32(out, 1, kGamepadKeyDataType);
    AppendBytes(out, 10, EncodeGamepadKey(state));
    // No input_control_type here: the official native client leaves field 17
    // unset on regular gamepad frames (only the type-31 reset frame carries
    // the input-source bitmask).
    return out;
}

std::vector<std::uint8_t> EncodeKeyboardChannel(std::uint32_t key_code, bool down) {
    std::vector<std::uint8_t> keyboard;
    AppendUInt32(keyboard, 1, key_code);          // RtcKeyboard.keyCode
    std::vector<std::uint8_t> out;
    out.reserve(16);
    AppendUInt32(out, 1, down ? kKeyboardDownDataType : kKeyboardUpDataType);
    AppendBytes(out, 2, keyboard);
    return out;
}

std::vector<std::uint8_t> EncodeMouseMoveChannel(double delta_x, double delta_y,
                                                 double cursor_x, double cursor_y) {
    std::vector<std::uint8_t> mouse;
    AppendDouble(mouse, 1, cursor_x);             // RtcMouse.cursorX
    AppendDouble(mouse, 2, cursor_y);             // RtcMouse.cursorY
    AppendDouble(mouse, 4, delta_x);              // RtcMouse.cursorRelativeX
    AppendDouble(mouse, 5, delta_y);              // RtcMouse.cursorRelativeY
    std::vector<std::uint8_t> out;
    out.reserve(64);
    AppendUInt32(out, 1, kMouseMoveDataType);
    AppendBytes(out, 3, mouse);
    return out;
}

std::vector<std::uint8_t> EncodeMouseMoveChannel(double delta_x, double delta_y) {
    return EncodeMouseMoveChannel(delta_x, delta_y, 0.0, 0.0);
}

// The official cg-sdk mouseButton(button, down, position) carries the cursor
// position and distinguishes buttons through the DataType (4/5 left, 6/7
// right); it never fills RtcMouse.buttonType.
std::vector<std::uint8_t> EncodeMouseButtonChannel(std::uint32_t button, bool down,
                                                   double cursor_x, double cursor_y) {
    std::vector<std::uint8_t> mouse;
    AppendDouble(mouse, 1, cursor_x);             // RtcMouse.cursorX
    AppendDouble(mouse, 2, cursor_y);             // RtcMouse.cursorY
    std::vector<std::uint8_t> out;
    out.reserve(48);
    const std::uint32_t type = button == kMouseButtonRight
        ? (down ? kMouseRightButtonDownDataType : kMouseRightButtonUpDataType)
        : (down ? kMouseLeftButtonDownDataType : kMouseLeftButtonUpDataType);
    AppendUInt32(out, 1, type);
    AppendBytes(out, 3, mouse);
    return out;
}

std::vector<std::uint8_t> EncodeGamepadListChannel(std::uint32_t vendor_id,
                                                   std::uint32_t product_id) {
    std::vector<std::uint8_t> list;
    AppendPackedUInt32(list, 1, {vendor_id});     // RtcGamepadList.vendorIdList
    AppendPackedUInt32(list, 2, {product_id});    // RtcGamepadList.productIdList
    std::vector<std::uint8_t> out;
    out.reserve(32);
    AppendUInt32(out, 1, kGamepadListDataType);
    AppendBytes(out, 11, list);
    return out;
}

std::vector<std::uint8_t> EncodeTouchEventListChannel(const std::vector<TouchEvent>& events) {
    if (events.empty() || events.size() > kTouchEventListLimit) return {};
    std::vector<std::uint8_t> list;
    for (const auto& event : events) {
        std::vector<std::uint8_t> encoded;
        AppendUInt32(encoded, 1, event.finger_id);   // RtcTouchEvent.fingerId
        AppendUInt32(encoded, 2, event.action);      // RtcTouchEvent.actionType
        AppendFloat(encoded, 3, static_cast<float>(event.x)); // RtcTouchEvent.pointX
        AppendFloat(encoded, 4, static_cast<float>(event.y)); // RtcTouchEvent.pointY
        // pressSizeX/pressSizeY are always zero in the official controller.
        if (event.primary) AppendUInt32(encoded, 7, 1); // RtcTouchEvent.isPrimary
        AppendBytes(list, 1, encoded);               // RtcTouchEventList.eventList
    }
    std::vector<std::uint8_t> out;
    out.reserve(40 + list.size());
    AppendUInt32(out, 1, kTouchEventListDataType);
    AppendBytes(out, 13, list);                      // RtcDataChannel.touchList
    // Field 17 stays unset: the official sendTouchEventList encoder writes
    // only the DataType and the touch list.
    return out;
}

std::vector<std::uint8_t> EncodeResetGameInputControlChannel(std::uint32_t input_types) {
    if (!input_types) return {};
    std::vector<std::uint8_t> out;
    out.reserve(12);
    AppendUInt32(out, 1, kResetGameInputControlDataType);
    AppendUInt32(out, 17, input_types);              // input_control_type bitmask
    return out;
}

std::vector<std::uint8_t> EncodeInputPacket(const std::vector<std::uint8_t>& channel_message) {
    if (channel_message.empty() ||
        channel_message.size() > std::numeric_limits<std::uint32_t>::max()) return {};
    std::vector<std::uint8_t> out;
    out.reserve(12 + channel_message.size());
    out.push_back(0x45);
    out.push_back(0x67);
    AppendU16Be(out, kRtcDataChannelCommand);
    AppendU16Be(out, 0); // Empty PacketHead for RTC input.
    AppendU32Be(out, static_cast<std::uint32_t>(channel_message.size()));
    out.insert(out.end(), channel_message.begin(), channel_message.end());
    out.push_back(0x89);
    out.push_back(0xab);
    return out;
}

std::vector<std::uint8_t> EncodeImuChannel(const MotionSample& sample) {
    if(!sample.valid || !FiniteMotion(sample.gyro) || !FiniteMotion(sample.gravity) ||
       !FiniteMotion(sample.acceleration) ||
       (sample.orientation && !UnitQuaternion(*sample.orientation))) return {};
    std::vector<std::uint8_t> imu,out;
    imu.reserve(80);out.reserve(88);
    for(unsigned i=0;i<3;++i) {
        AppendFloat(imu,1+i,sample.gyro[i]);
        // Java TYPE_GRAVITY is converted to negative unit gravity before JNI.
        AppendFloat(imu,7+i,-sample.gravity[i]);
        AppendFloat(imu,10+i,sample.acceleration[i]);
    }
    if(sample.orientation) {
        // APK ImuEventListener stores Android [w,x,y,z] at JNI array [9..12].
        // ClientCoreImpl::sendImuEvent copies [36/40/44/48] to member offsets
        // [64/68/72/76]; RtcImuEvent serializer emits tags 6d/75/7d/85 01.
        // Preserve this observed wire order; field *names* are misleading.
        const auto& q=*sample.orientation;
        AppendFloat(imu,13,q.w);AppendFloat(imu,14,q.x);
        AppendFloat(imu,15,q.y);AppendFloat(imu,16,q.z);
    }
    AppendUInt32(imu,20,1);
    AppendUInt32(out,1,22);AppendBytes(out,14,imu);
    return out;
}

std::vector<std::uint8_t> EncodeGamepadPacket(const GamepadState& state) {
    return EncodeInputPacket(EncodeGamepadChannel(state));
}

bool IsValidControlPacket(const std::vector<std::uint8_t>& packet) {
    if (packet.size() < 12 || packet[0] != 0x45 || packet[1] != 0x67 ||
        packet[packet.size() - 2] != 0x89 || packet.back() != 0xab)
        return false;
    const auto header_length = ReadU16Be(packet, 4);
    const auto message_length = ReadU32Be(packet, 6);
    const auto available = packet.size() - 12;
    return header_length <= available &&
           message_length == available - header_length;
}

} // namespace cloudgenshin::input
