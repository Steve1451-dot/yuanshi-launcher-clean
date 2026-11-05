#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include "mihoyo/motion_input.hpp"

namespace cloudgenshin::input {

// Semantic Switch controls, deliberately independent of libnx for host tests.
struct SwitchControllerState {
    bool dpad_up = false;
    bool dpad_down = false;
    bool dpad_left = false;
    bool dpad_right = false;
    bool plus = false;
    bool minus = false;
    bool stick_left = false;
    bool stick_right = false;
    bool l = false;
    bool r = false;
    bool zl = false;
    bool zr = false;
    bool a = false;
    bool b = false;
    bool x = false;
    bool y = false;
    std::int32_t left_x = 0;
    std::int32_t left_y = 0;
    std::int32_t right_x = 0;
    std::int32_t right_y = 0;
};

// The official Android client only classifies Xbox (vendor 1118/0x045e) and
// PlayStation (1356/0x054c) pads for the virtual gamepad. Announce and report
// an Xbox 360 controller so the pod and game expose controller prompts.
constexpr std::uint32_t kXboxVendorId = 0x045e;
constexpr std::uint32_t kXboxProductId = 0x028e;

struct GamepadState {
    std::uint32_t vendor_id = 0;
    std::uint32_t product_id = 0;
    std::uint32_t buttons = 0;
    std::uint32_t left_trigger = 0;
    std::uint32_t right_trigger = 0;
    std::int32_t left_thumb_x = 0;
    std::int32_t left_thumb_y = 0;
    std::int32_t right_thumb_x = 0;
    std::int32_t right_thumb_y = 0;
    std::int64_t rtt_trace_send_time = 0;
};

// Periodic neutral reports maintain the virtual pad, but are not user activity.
// Apply an activity-only dead zone without changing the public input packets.
inline bool HasGamepadActivity(const GamepadState& state) {
    const auto moved = [](std::int32_t axis) {return axis < -6000 || axis > 6000;};
    return state.buttons || state.left_trigger || state.right_trigger ||
        moved(state.left_thumb_x) || moved(state.left_thumb_y) ||
        moved(state.right_thumb_x) || moved(state.right_thumb_y);
}

// Official TouchController events. pointX/pointY are normalised to [0,1]
// against the video rectangle, exactly like the mobile web client.
struct TouchEvent {
    std::uint32_t finger_id = 0;
    std::uint32_t action = 0;
    double x = 0.0;
    double y = 0.0;
    bool primary = false;
};

constexpr std::uint16_t kRtcDataChannelCommand = 20010;

// proto.common.DataType values used by the official cg-sdk RTC data channel.
constexpr std::uint32_t kKeyboardDownDataType = 1;
constexpr std::uint32_t kKeyboardUpDataType = 2;
constexpr std::uint32_t kMouseMoveDataType = 3;
constexpr std::uint32_t kGamepadKeyDataType = 17;
constexpr std::uint32_t kGamepadListDataType = 18;
constexpr std::uint32_t kTouchEventListDataType = 21;
constexpr std::uint32_t kMouseLeftButtonDownDataType = 4;
constexpr std::uint32_t kMouseLeftButtonUpDataType = 5;
constexpr std::uint32_t kMouseRightButtonDownDataType = 6;
constexpr std::uint32_t kMouseRightButtonUpDataType = 7;

// proto.enumerate.GameControlMode carried by the server->client
// SyncGameControlMode command (20037): 0 default, 1 touch, 2 keyboard,
// 3 gamepad. The official native client never sends this enum upstream.
constexpr std::uint32_t kGameControlModeDefault = 0;
constexpr std::uint32_t kGameControlModeTouch = 1;
constexpr std::uint32_t kGameControlModeKeyboard = 2;
constexpr std::uint32_t kGameControlModeGamepad = 3;

// proto.common.DataType 31 RESET_GAME_INPUT_CONTROL. RtcDataChannel field 17
// (input_control_type) is a *bitmask* of ClientCore.GameContorlType, used only
// on this frame: the official app calls resetGameInputControl(TOUCH_SCREEN)
// when its UI takes over, and the native core sends {type=31, field17=8}.
// Regular type-17/18/21 frames leave field 17 at 0, exactly like the official
// client (ClientCoreImpl::onGamepadPressed / onGamepadDevices /
// sendTouchEventList never write it).
constexpr std::uint32_t kResetGameInputControlDataType = 31;
constexpr std::uint32_t kInputControlKeyboard = 1;
constexpr std::uint32_t kInputControlMouse = 2;
constexpr std::uint32_t kInputControlGamepad = 4;
constexpr std::uint32_t kInputControlTouchScreen = 8;

// proto.common.TouchActionType used by the official mobile web client.
constexpr std::uint32_t kTouchActionDown = 0;
constexpr std::uint32_t kTouchActionUp = 1;
constexpr std::uint32_t kTouchActionMove = 2;
// The official SDK rejects RtcTouchEventList batches larger than 10 events.
constexpr std::size_t kTouchEventListLimit = 10;

// proto.common.RtcMouseButton.
constexpr std::uint32_t kMouseButtonLeft = 1;
constexpr std::uint32_t kMouseButtonRight = 2;

// WindowsVirtualKeycode values used by the official web client's key
// translation (GenshinImpactKeyEvent fills RtcKeyboard.keyCode with these).
constexpr std::uint32_t kVkTab = 0x09;
constexpr std::uint32_t kVkReturn = 0x0d;
constexpr std::uint32_t kVkShift = 0x10;
constexpr std::uint32_t kVkEscape = 0x1b;
constexpr std::uint32_t kVkSpace = 0x20;
constexpr std::uint32_t kVkKey1 = 0x31;
constexpr std::uint32_t kVkKey2 = 0x32;
constexpr std::uint32_t kVkKey3 = 0x33;
constexpr std::uint32_t kVkKey4 = 0x34;
constexpr std::uint32_t kVkKeyA = 0x41;
constexpr std::uint32_t kVkKeyD = 0x44;
constexpr std::uint32_t kVkKeyE = 0x45;
constexpr std::uint32_t kVkKeyF = 0x46;
constexpr std::uint32_t kVkKeyM = 0x4d;
constexpr std::uint32_t kVkKeyQ = 0x51;
constexpr std::uint32_t kVkKeyS = 0x53;
constexpr std::uint32_t kVkKeyW = 0x57;

// Preserve physical positions by default: Switch B(bottom)->XInput A,
// A(right)->B, Y(left)->X, X(top)->Y. Pass "Switch" to match labels instead
// (A->A, B->B, X->X, Y->Y). The caller supplies a monotonic millisecond
// timestamp.
GamepadState ToGamepadState(const SwitchControllerState& state,
                            std::int64_t monotonic_millis,
                            std::uint32_t vendor_id = kXboxVendorId,
                            std::uint32_t product_id = kXboxProductId,
                            std::string_view controller_layout = "Xbox");

// RtcDataChannel field 1 is the DataType; the payload field follows the
// official cg-sdk map: keyboard = 2, mouse = 3, gamepad = 10, gamepadList = 11.
std::vector<std::uint8_t> EncodeGamepadChannel(const GamepadState& state);
std::vector<std::uint8_t> EncodeKeyboardChannel(std::uint32_t key_code, bool down);
std::vector<std::uint8_t> EncodeMouseMoveChannel(double delta_x, double delta_y);
std::vector<std::uint8_t> EncodeMouseMoveChannel(double delta_x, double delta_y,
                                                 double cursor_x, double cursor_y);
std::vector<std::uint8_t> EncodeMouseButtonChannel(std::uint32_t button, bool down,
                                                  double cursor_x, double cursor_y);
std::vector<std::uint8_t> EncodeGamepadListChannel(std::uint32_t vendor_id,
                                                   std::uint32_t product_id);
// RtcDataChannel type 21 with field 13 = RtcTouchEventList. The official SDK
// rejects batches larger than 10 events; pressSize is always zero.
std::vector<std::uint8_t> EncodeTouchEventListChannel(const std::vector<TouchEvent>& events);
// RtcDataChannel type 31 with field 17 = bitmask of input sources whose held
// state must be cleared (kInputControl*). Empty mask yields an empty frame.
std::vector<std::uint8_t> EncodeResetGameInputControlChannel(std::uint32_t input_types);
// APK 7.1.0: DataType 22, field 14 RtcImuEvent. Sensor coordinates stay
// portrait-native; orientation_type=1 is Android Surface.ROTATION_90.
// When available, quaternion is APK w/x/y/z order in fields 13/14/15/16,
// despite the descriptor's x/y/z/w names. No fabricated pose or unbiased gyro.
std::vector<std::uint8_t> EncodeImuChannel(const MotionSample& sample);

// The public SDK's empty-header 0x4567/0x89AB packet for RTC data channels.
std::vector<std::uint8_t> EncodeInputPacket(const std::vector<std::uint8_t>& channel_message);
std::vector<std::uint8_t> EncodeGamepadPacket(const GamepadState& state);
bool IsValidControlPacket(const std::vector<std::uint8_t>& packet);

} // namespace cloudgenshin::input
