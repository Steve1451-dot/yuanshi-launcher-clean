#include "mihoyo/input_packet.hpp"

#include <cassert>
#include <array>
#include <cstdint>
#include <utility>
#include <vector>

int main() {
    using namespace cloudgenshin::input;

    SwitchControllerState controls;
    controls.b = true;   // Physical bottom button -> XInput A.
    controls.zl = true;
    controls.left_x = 40000;
    controls.left_y = -40000;
    const auto mapped = ToGamepadState(controls, -1);
    assert(mapped.vendor_id == 0x045e && mapped.product_id == 0x028e);
    assert(mapped.buttons == 0x1000);
    assert(mapped.left_trigger == 255 && mapped.right_trigger == 0);
    assert(mapped.left_thumb_x == 32767 && mapped.left_thumb_y == -32767);
    assert(mapped.rtt_trace_send_time == 0);
    const std::array<std::pair<bool SwitchControllerState::*,std::uint32_t>,16> button_map{{
        {&SwitchControllerState::dpad_up,0x0001},{&SwitchControllerState::dpad_down,0x0002},
        {&SwitchControllerState::dpad_left,0x0004},{&SwitchControllerState::dpad_right,0x0008},
        {&SwitchControllerState::plus,0x0010},{&SwitchControllerState::minus,0x0020},
        {&SwitchControllerState::stick_left,0x0040},{&SwitchControllerState::stick_right,0x0080},
        {&SwitchControllerState::l,0x0100},{&SwitchControllerState::r,0x0200},
        {&SwitchControllerState::b,0x1000},{&SwitchControllerState::a,0x2000},
        {&SwitchControllerState::y,0x4000},{&SwitchControllerState::x,0x8000},
        {&SwitchControllerState::zl,0},{&SwitchControllerState::zr,0}}};
    for(const auto& [member,mask]:button_map) {
        SwitchControllerState single;single.*member=true;
        const auto pad=ToGamepadState(single,0);
        assert(pad.buttons==mask);
        assert(pad.left_trigger==(member==&SwitchControllerState::zl?255U:0U));
        assert(pad.right_trigger==(member==&SwitchControllerState::zr?255U:0U));
    }
    // "Switch" keeps the printed labels instead of physical positions.
    const std::array<std::pair<bool SwitchControllerState::*,std::uint32_t>,4> switch_labels{{
        {&SwitchControllerState::a,0x1000},{&SwitchControllerState::b,0x2000},
        {&SwitchControllerState::x,0x4000},{&SwitchControllerState::y,0x8000}}};
    for(const auto& [member,mask]:switch_labels) {
        SwitchControllerState single;single.*member=true;
        assert(ToGamepadState(single,0,kXboxVendorId,kXboxProductId,"Switch").buttons==mask);
    }
    // Only the exact "Switch" value changes behaviour; unknown settings keep
    // the physical-position default.
    SwitchControllerState face_a;face_a.a=true;
    assert(ToGamepadState(face_a,0,kXboxVendorId,kXboxProductId,"switch").buttons==0x2000);
    assert(ToGamepadState(face_a,0,kXboxVendorId,kXboxProductId,"").buttons==0x2000);

    GamepadState minimal;
    minimal.vendor_id = 0x045e;
    minimal.product_id = 0x028e;
    minimal.buttons = 0x1000;
    minimal.left_trigger = 255;
    const std::vector<std::uint8_t> expected{
        0x45, 0x67, 0x4e, 0x2a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10,
        0x08, 0x11, 0x52, 0x0c,
        0x08, 0xde, 0x08,
        0x10, 0x8e, 0x05,
        0x18, 0x80, 0x20,
        0x20, 0xff, 0x01,
        0x89, 0xab,
    };
    const auto packet = EncodeGamepadPacket(minimal);
    assert(packet == expected);
    assert(IsValidControlPacket(packet));
    auto damaged = packet;
    damaged[9] = 0x0f;
    assert(!IsValidControlPacket(damaged));
    damaged = packet;
    damaged.back() = 0;
    assert(!IsValidControlPacket(damaged));
    assert(!IsValidControlPacket({}));

    // Official keyboard/mouse/gamepad-list channel frames (cg-sdk DataType map).
    assert((EncodeKeyboardChannel(kVkKeyW, true) ==
            std::vector<std::uint8_t>{0x08,0x01,0x12,0x02,0x08,0x57}));
    assert((EncodeKeyboardChannel(kVkSpace, false) ==
            std::vector<std::uint8_t>{0x08,0x02,0x12,0x02,0x08,0x20}));
    // Official mouse-button frames: DataType distinguishes button and edge
    // (cg-sdk MouseButtonType -> MOUSE_LBUTTON_DOWN 4 ... MOUSE_RBUTTON_UP 7);
    // the RtcMouse submessage carries only the cursor position. Zero cursor
    // coordinates are omitted, so the submessage stays an empty field 3.
    assert((EncodeMouseButtonChannel(kMouseButtonLeft, true, 0.0, 0.0) ==
            std::vector<std::uint8_t>{0x08,0x04,0x1a,0x00}));
    assert((EncodeMouseButtonChannel(kMouseButtonRight, false, 0.0, 0.0) ==
            std::vector<std::uint8_t>{0x08,0x07,0x1a,0x00}));
    // Absolute-cursor variant used by the continue click (frame centre).
    assert((EncodeMouseButtonChannel(kMouseButtonLeft, true, 640.0, 360.0) ==
            std::vector<std::uint8_t>{
        0x08,0x04,0x1a,0x12,
        0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x84,0x40,
        0x11,0x00,0x00,0x00,0x00,0x00,0x80,0x76,0x40}));
    // Zero cursor coordinates are omitted; -2.5 encodes as 0xc004000000000000.
    assert((EncodeMouseMoveChannel(1.5, -2.5) == std::vector<std::uint8_t>{
        0x08,0x03,0x1a,0x12,
        0x21,0x00,0x00,0x00,0x00,0x00,0x00,0xf8,0x3f,
        0x29,0x00,0x00,0x00,0x00,0x00,0x00,0x04,0xc0}));
    // The absolute-cursor variant used to click through the continue screen.
    assert((EncodeMouseMoveChannel(2.0, 2.0, 640.0, 360.0) == std::vector<std::uint8_t>{
        0x08,0x03,0x1a,0x24,
        0x09,0x00,0x00,0x00,0x00,0x00,0x00,0x84,0x40,
        0x11,0x00,0x00,0x00,0x00,0x00,0x80,0x76,0x40,
        0x21,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40,
        0x29,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x40}));
    // Packed repeated uint32 for the Xbox 360 Controller identity.
    assert((EncodeGamepadListChannel(0x045e, 0x028e) == std::vector<std::uint8_t>{
        0x08,0x12,0x5a,0x08,0x0a,0x02,0xde,0x08,0x12,0x02,0x8e,0x05}));
    // Official TouchController frame: DataType 21, RtcDataChannel field 13,
    // RtcTouchEventList field 1 repeated. pointX/pointY are normalised and are
    // proto float (wire type 5), not double.
    assert((EncodeTouchEventListChannel({{0,kTouchActionDown,0.5,0.25,true}}) ==
            std::vector<std::uint8_t>{
        0x08,0x15,0x6a,0x0e,0x0a,0x0c,
        0x1d,0x00,0x00,0x00,0x3f,
        0x25,0x00,0x00,0x80,0x3e,
        0x38,0x01}));
    // Up uses action 1; fingerId 1 is written, primary false is omitted.
    assert((EncodeTouchEventListChannel({{1,kTouchActionUp,0.5,0.5,false}}) ==
            std::vector<std::uint8_t>{
        0x08,0x15,0x6a,0x10,0x0a,0x0e,
        0x08,0x01,0x10,0x01,
        0x1d,0x00,0x00,0x00,0x3f,
        0x25,0x00,0x00,0x00,0x3f}));
    // The only frame that carries input_control_type: type 31 plus the
    // ClientCore.GameContorlType bitmask (TOUCH_SCREEN = 8).
    assert((EncodeResetGameInputControlChannel(kInputControlTouchScreen) ==
            std::vector<std::uint8_t>{0x08,0x1f,0x88,0x01,0x08}));
    assert((EncodeResetGameInputControlChannel(kInputControlMouse | kInputControlKeyboard) ==
            std::vector<std::uint8_t>{0x08,0x1f,0x88,0x01,0x03}));
    assert(EncodeResetGameInputControlChannel(0).empty());
    assert(EncodeTouchEventListChannel({}).empty());
    assert(EncodeTouchEventListChannel(std::vector<TouchEvent>(11)).empty());
    const auto wrapped = EncodeInputPacket(EncodeKeyboardChannel(kVkSpace, false));
    assert((wrapped == std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x2a,0x00,0x00,0x00,0x00,0x00,0x06,
        0x08,0x02,0x12,0x02,0x08,0x20,0x89,0xab}));
    assert(IsValidControlPacket(wrapped));
    assert(EncodeInputPacket({}).empty());
    auto inactive = ToGamepadState({}, 123456);
    assert(!HasGamepadActivity(inactive));
    inactive.left_thumb_x=1000; assert(!HasGamepadActivity(inactive));
    inactive.right_thumb_y=-6001; assert(HasGamepadActivity(inactive));
    SwitchControllerState pressed; pressed.a=true;
    assert(HasGamepadActivity(ToGamepadState(pressed,0)));
}
