#include "endfield/wl_input.hpp"

#include "endfield/wl_proto.hpp"
#include <array>
#include <utility>

namespace cloudgenshin::endfield::input {
namespace {

// Complete event.code -> VK table of the official web client (@3395956).
// Reproduced verbatim, including entries where both sides share a VK and the
// Semicolon/Comma collision (188).
struct CodeToVk {
    std::string_view code;
    std::int32_t vk;
};

constexpr CodeToVk kCodeToVk[] = {
    {"Backquote", 192},   {"Backslash", 220},   {"Backspace", 8},
    {"BracketLeft", 219}, {"BracketRight", 221}, {"Comma", 188},
    {"Digit0", 48},       {"Digit1", 49},       {"Digit2", 50},
    {"Digit3", 51},       {"Digit4", 52},       {"Digit5", 53},
    {"Digit6", 54},       {"Digit7", 55},       {"Digit8", 56},
    {"Digit9", 57},       {"Enter", 13},        {"Equal", 187},
    {"F1", 112},          {"F2", 113},          {"F3", 114},
    {"F4", 115},          {"F5", 116},          {"F6", 117},
    {"F7", 118},          {"F8", 119},          {"F9", 120},
    {"F10", 121},         {"F11", 122},         {"F12", 123},
    {"Tab", 9},
    {"KeyA", 65},         {"KeyB", 66},         {"KeyC", 67},
    {"KeyD", 68},         {"KeyE", 69},         {"KeyF", 70},
    {"KeyG", 71},         {"KeyH", 72},         {"KeyI", 73},
    {"KeyJ", 74},         {"KeyK", 75},         {"KeyL", 76},
    {"KeyM", 77},         {"KeyN", 78},         {"KeyO", 79},
    {"KeyP", 80},         {"KeyQ", 81},         {"KeyR", 82},
    {"KeyS", 83},         {"KeyT", 84},         {"KeyU", 85},
    {"KeyV", 86},         {"KeyW", 87},         {"KeyX", 88},
    {"KeyY", 89},         {"KeyZ", 90},
    {"Minus", 189},       {"Period", 190},      {"Quote", 222},
    {"Semicolon", 188},   {"Slash", 191},       {"Space", 32},
    {"AltLeft", 18},      {"AltRight", 18},
    {"AudioVolumeDown", 174}, {"AudioVolumeMute", 173}, {"AudioVolumeUp", 175},
    {"CapsLock", 20},     {"ControlLeft", 17},  {"ControlRight", 17},
    {"ContextMenu", 93},  {"Delete", 46},       {"End", 35},
    {"Help", 47},         {"Home", 36},         {"Insert", 45},
    {"MetaLeft", 91},     {"MetaRight", 92},    {"NumLock", 136},
    {"PageDown", 34},     {"PageUp", 33},       {"Pause", 19},
    {"ScrollLock", 137},  {"ShiftLeft", 16},    {"ShiftRight", 16},
    {"ArrowDown", 40},    {"ArrowLeft", 37},    {"ArrowRight", 39},
    {"ArrowUp", 38},      {"Escape", 27},       {"PrintScreen", 44},
    {"IntlBackslash", 220}, {"IntlRo", 193},    {"IntlYen", 165},
    {"Numpad0", 96},      {"Numpad1", 97},      {"Numpad2", 98},
    {"Numpad3", 99},      {"Numpad4", 100},     {"Numpad5", 101},
    {"Numpad6", 102},     {"Numpad7", 103},     {"Numpad8", 104},
    {"Numpad9", 105},     {"NumpadAdd", 107},   {"NumpadBackspace", 8},
    {"NumpadClear", 12},  {"NumpadClearEntry", 12}, {"NumpadComma", 188},
    {"NumpadDecimal", 110}, {"NumpadDivide", 111}, {"NumpadEnter", 13},
    {"NumpadEqual", 187}, {"NumpadMultiply", 106}, {"NumpadParenLeft", 57},
    {"NumpadParenRight", 48}, {"NumpadSubtract", 109},
};

std::vector<std::uint8_t> Send(const OneInputOpData& op) { return CtrlMsg(op); }
void AppendVarint(std::vector<std::uint8_t>& out,std::uint64_t value) {
    while(value>=128) {out.push_back(static_cast<std::uint8_t>(value)|0x80);value>>=7;}
    out.push_back(static_cast<std::uint8_t>(value));
}

}  // namespace

std::optional<std::int32_t> VkFromCode(std::string_view code) {
    for (const auto& entry : kCodeToVk) {
        if (entry.code == code) return entry.vk;
    }
    return std::nullopt;
}

std::vector<std::uint8_t> EncodeKey(std::int32_t vk, bool down, std::int32_t index) {
    OneInputOpData op;
    op.input_op = vk;
    op.input_state = down ? kStateDown : kStateUp;
    op.op_device = kDeviceKeyboard;
    op.xinput_user_index = index;
    return Send(op);
}

std::vector<std::uint8_t> EncodeXInputConnection(bool connected,std::int32_t index) {
    if(index<0 || index>3) return {};
    // WLCGV2.InputOPData.op_list(1) -> OneInputOPData{op_device(1):3,
    // state(11):Connect{connectstate(1):bool,userindex(2):int32,padtype(3):0}}.
    // jspb writes explicitly set false/zero values; retain all three fields.
    SimpleCmd command;
    command.cmd="CtrlMsgV2";
    command.data=std::vector<std::uint8_t>{0x0a,0x0a,0x08,0x03,0x5a,0x06,
        0x08,static_cast<std::uint8_t>(connected),0x10,static_cast<std::uint8_t>(index),0x18,0x00};
    return command.Serialize();
}

std::vector<std::uint8_t> EncodeXInputStateV2(std::int32_t index,std::int32_t buttons,
    std::int32_t left_trigger,std::int32_t right_trigger,std::int32_t lx,std::int32_t ly,
    std::int32_t rx,std::int32_t ry) {
    if(index<0 || index>3) return {};
    const std::array<std::int32_t,8> values{buttons,left_trigger,right_trigger,lx,ly,rx,ry,index};
    std::vector<std::uint8_t> state;
    for(std::size_t i=0;i<values.size();++i) {
        AppendVarint(state,(i+1)<<3);
        AppendVarint(state,static_cast<std::uint64_t>(static_cast<std::int64_t>(values[i])));
    }
    // OneInputOPData{op_device(1):3, game_pad(5):GamePad}, repeated field 1.
    std::vector<std::uint8_t> op{0x08,0x03,0x2a};
    AppendVarint(op,state.size());op.insert(op.end(),state.begin(),state.end());
    std::vector<std::uint8_t> list{0x0a};
    AppendVarint(list,op.size());list.insert(list.end(),op.begin(),op.end());
    SimpleCmd command;command.cmd="CtrlMsgV2";command.data=std::move(list);
    return command.Serialize();
}

std::vector<std::uint8_t> EncodeMouseMove(std::int32_t cursor_x, std::int32_t cursor_y,
                                          std::int32_t mouse_x, std::int32_t mouse_y,
                                          std::int32_t index) {
    OneInputOpData op;
    op.input_op = kMouseMove;
    op.input_state = kStateDefault;
    op.cursor_pos = CoordinatePos{cursor_x, cursor_y};
    op.mouse_pos = CoordinatePos{mouse_x, mouse_y};
    op.op_device = kDeviceMouse;
    op.xinput_user_index = index;
    return Send(op);
}

std::vector<std::uint8_t> EncodeMouseButton(std::int32_t button_op, bool down,
                                            std::int32_t cursor_x, std::int32_t cursor_y,
                                            std::int32_t mouse_x, std::int32_t mouse_y,
                                            std::int32_t index) {
    OneInputOpData op;
    op.input_op = button_op;
    op.input_state = down ? kStateDown : kStateUp;
    op.cursor_pos = CoordinatePos{cursor_x, cursor_y};
    op.mouse_pos = CoordinatePos{mouse_x, mouse_y};
    op.op_device = kDeviceMouse;
    op.xinput_user_index = index;
    return Send(op);
}

std::vector<std::uint8_t> EncodeMouseWheel(std::int32_t delta,
                                         std::int32_t cursor_x, std::int32_t cursor_y,
                                         std::int32_t mouse_x, std::int32_t mouse_y,
                                         std::int32_t index) {
    OneInputOpData op;
    op.input_op = kMouseWheel;
    op.input_state = kStateDefault;
    op.value = delta;
    op.cursor_pos = CoordinatePos{cursor_x, cursor_y};
    op.mouse_pos = CoordinatePos{mouse_x, mouse_y};
    op.op_device = kDeviceMouse;
    op.xinput_user_index = index;
    return Send(op);
}

std::vector<std::uint8_t> EncodeXInputButtons(std::int32_t index, std::int32_t buttons) {
    OneInputOpData op;
    op.input_op = kXInputButtons;
    op.value = buttons;
    op.op_device = kDeviceXInput;
    op.xinput_user_index = index;
    return Send(op);
}

std::vector<std::uint8_t> EncodeXInputTrigger(std::int32_t index, bool left,
                                              std::int32_t value) {
    OneInputOpData op;
    op.input_op = left ? kXInputLeftTrigger : kXInputRightTrigger;
    op.value = value;
    op.op_device = kDeviceXInput;
    op.xinput_user_index = index;
    return Send(op);
}

std::vector<std::uint8_t> EncodeXInputAxis(std::int32_t index, std::int32_t axis_op,
                                           std::int32_t value) {
    OneInputOpData op;
    op.input_op = axis_op;
    op.value = value;
    op.op_device = kDeviceXInput;
    op.xinput_user_index = index;
    return Send(op);
}

std::vector<std::uint8_t> KeepAliveFrame() {
    OneInputOpData op;
    op.input_op = kInputDefault;
    op.value = 1;
    op.cursor_pos = CoordinatePos{1, 1};
    op.op_device = 999;
    op.xinput_user_index = 0;
    return Send(op);
}

}  // namespace cloudgenshin::endfield::input
