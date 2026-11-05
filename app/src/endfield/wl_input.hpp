#pragma once

// Input encoding for the Endfield cloud session: OneInputOPData ops carried
// inside SimpleCmd{cmd:"CtrlMsg"} messages on the "welink" DataChannel.
//
// Values replicate the official web client tables (findings 9.1/9.2/9.4):
//  - input_state: DEFAULT=1, DOWN=2, UP=3 (VK_UNTOGGLED=4, VK_TOGGLED=5);
//  - op_device: KEYBOARD=1, MOUSE=2, XINPUT=3, GAMEPAD=4, TOUCH=16;
//  - mouse/XInput op codes and the XInput button bit values of the official
//    button table g[] (@1895260: A=4096 B=8192 X=16384 Y=32768 LB=256 RB=512
//    BACK=32 START=16 LTHUMB=64 RTHUMB=128 dpad=1/2/4/8);
//  - the complete JS event.code -> Windows VK table (@3395956), including its
//    quirks: Semicolon->188, AltLeft/AltRight->18, ControlLeft/Right->17,
//    ShiftLeft/Right->16 (findings 9.3 line 223 corrected by the bundle),
//    IntlRo/IntlYen/NumpadClear/Paren keys.
//
// Wire tags (Keyboard/mouseEvent/mousemove/gamePadEvent/keepAliveForGame) are
// local web-client log labels, not wire fields.

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace cloudgenshin::endfield::input {

// input_state (findings 9.1).
inline constexpr int kStateDefault = 1;
inline constexpr int kStateDown = 2;
inline constexpr int kStateUp = 3;

// op_device (findings 9.1).
inline constexpr int kDeviceKeyboard = 1;
inline constexpr int kDeviceMouse = 2;
inline constexpr int kDeviceXInput = 3;
inline constexpr int kDeviceGamepad = 4;
inline constexpr int kDeviceTouch = 16;

// Mouse op codes (findings 9.2).
inline constexpr int kMouseButtonLeft = 512;
inline constexpr int kMouseButtonMiddle = 513;
inline constexpr int kMouseButtonRight = 514;
inline constexpr int kMouseWheel = 515;
inline constexpr int kMouseMove = 516;
inline constexpr int kMouseButtonBack = 518;
inline constexpr int kMouseButtonForward = 519;

// XInput op codes (findings 9.2).
inline constexpr int kXInputButtons = 1024;
inline constexpr int kXInputLeftTrigger = 1025;
inline constexpr int kXInputRightTrigger = 1026;
inline constexpr int kXInputThumbLeftX = 1027;
inline constexpr int kXInputThumbLeftY = 1028;
inline constexpr int kXInputThumbRightX = 1029;
inline constexpr int kXInputThumbRightY = 1030;

// Default / keep-alive op (findings 9.2 / 14.4).
inline constexpr int kInputDefault = 0;
inline constexpr int kTouchOp = 1152;

// XInput button bits (official web table g[]).
inline constexpr int kXButtonA = 4096;
inline constexpr int kXButtonB = 8192;
inline constexpr int kXButtonX = 16384;
inline constexpr int kXButtonY = 32768;
inline constexpr int kXButtonLeftBumper = 256;
inline constexpr int kXButtonRightBumper = 512;
inline constexpr int kXButtonBack = 32;
inline constexpr int kXButtonStart = 16;
inline constexpr int kXButtonLeftThumb = 64;
inline constexpr int kXButtonRightThumb = 128;
inline constexpr int kXButtonDpadUp = 1;
inline constexpr int kXButtonDpadDown = 2;
inline constexpr int kXButtonDpadLeft = 4;
inline constexpr int kXButtonDpadRight = 8;

// JS KeyboardEvent.code -> Windows VK code; std::nullopt for unknown codes.
std::optional<std::int32_t> VkFromCode(std::string_view code);

// --- CtrlMsg frames (complete welink messages) -----------------------------

// Official R()/gamePadEvent: WLCGV2 CtrlMsgV2 state.Connect. This is the
// transport's Xbox type 0, separate from GameBridge's UI type 11.
std::vector<std::uint8_t> EncodeXInputConnection(bool connected,std::int32_t index=0);

// p({device:KEYBOARD, index, state:DOWN|UP, action:VK}) (findings 9.4).
std::vector<std::uint8_t> EncodeKey(std::int32_t vk, bool down, std::int32_t index = 0);

// p({cursorPos, mousePos, device:MOUSE, index, action:MOUSE_MOV, state:DEFAULT}).
std::vector<std::uint8_t> EncodeMouseMove(std::int32_t cursor_x, std::int32_t cursor_y,
                                          std::int32_t mouse_x, std::int32_t mouse_y,
                                          std::int32_t index = 0);

// Button events carry their own cursor position, just like the official k().
// A preceding move does not replace these required event coordinates.
std::vector<std::uint8_t> EncodeMouseButton(std::int32_t button_op, bool down,
                                            std::int32_t cursor_x, std::int32_t cursor_y,
                                            std::int32_t mouse_x = 0, std::int32_t mouse_y = 0,
                                            std::int32_t index = 0);

// p({device:MOUSE, index, action:MOUSE_WHEEL, value:delta}).
std::vector<std::uint8_t> EncodeMouseWheel(std::int32_t delta,
                                         std::int32_t cursor_x, std::int32_t cursor_y,
                                         std::int32_t mouse_x = 0, std::int32_t mouse_y = 0,
                                         std::int32_t index = 0);

// p({device:XINPUT, index, action:BUTTONS, value:bitmask}).
std::vector<std::uint8_t> EncodeXInputButtons(std::int32_t index, std::int32_t buttons);
// Official M()/gamePadEventXbox: complete WLCGV2 GamePad for input_ver>=2.
std::vector<std::uint8_t> EncodeXInputStateV2(std::int32_t index,std::int32_t buttons,
    std::int32_t left_trigger,std::int32_t right_trigger,std::int32_t lx,std::int32_t ly,
    std::int32_t rx,std::int32_t ry);

// p({device:XINPUT, index, action:LEFT|RIGHT_TRIGGER, value}).
std::vector<std::uint8_t> EncodeXInputTrigger(std::int32_t index, bool left,
                                              std::int32_t value);

// p({device:XINPUT, index, action:axis_op, value}) with a kXInputThumb* op.
std::vector<std::uint8_t> EncodeXInputAxis(std::int32_t index, std::int32_t axis_op,
                                           std::int32_t value);

// p({cursorPos:{x:1,y:1}, device:999, index:0, action:0, value:1})
// tag keepAliveForGame (findings 14.4).
std::vector<std::uint8_t> KeepAliveFrame();

}  // namespace cloudgenshin::endfield::input
