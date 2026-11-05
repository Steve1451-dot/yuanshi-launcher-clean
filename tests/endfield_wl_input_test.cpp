#include "endfield/wl_input.hpp"
#include "endfield/wl_proto.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace cloudgenshin::endfield;
    // Golden CG.PROTO.InputOPData bytes for the official mouse k()/C()
    // field assignments. Both absolute and relative positions are present,
    // even when relative movement and the user index are zero.
    const std::vector<std::uint8_t> down={
        0x0a,0x17,0x08,0x80,0x04,0x10,0x02,
        0x22,0x06,0x08,0x80,0x05,0x10,0xe8,0x02,
        0x2a,0x04,0x08,0x00,0x10,0x00,0x30,0x02,0x38,0x00};
    const auto press=SimpleCmd::Parse(input::EncodeMouseButton(input::kMouseButtonLeft,true,640,360));
    assert(press && press->cmd=="CtrlMsg" && press->data==down);
    auto up=down;up[6]=0x03;
    const auto release=SimpleCmd::Parse(input::EncodeMouseButton(input::kMouseButtonLeft,false,640,360));
    assert(release && release->data==up);
    const std::vector<std::uint8_t> wheel={
        0x0a,0x19,0x08,0x83,0x04,0x10,0x01,0x18,0x78,
        0x22,0x06,0x08,0x80,0x05,0x10,0xe8,0x02,
        0x2a,0x04,0x08,0x00,0x10,0x00,0x30,0x02,0x38,0x00};
    const auto scroll=SimpleCmd::Parse(input::EncodeMouseWheel(120,640,360));
    assert(scroll && scroll->data==wheel);
    const std::vector<std::uint8_t> attached={
        0x0a,0x09,'C','t','r','l','M','s','g','V','2',0x1a,0x0c,
        0x0a,0x0a,0x08,0x03,0x5a,0x06,0x08,0x01,0x10,0x00,0x18,0x00};
    assert(input::EncodeXInputConnection(true)==attached);
    auto detached=attached;detached[20]=0x00;detached[22]=0x03;
    assert(input::EncodeXInputConnection(false,3)==detached);
    assert(input::EncodeXInputConnection(true,-1).empty());
    assert(input::EncodeXInputConnection(true,4).empty());
    // Independent generated-protobuf layout: complete GamePad includes all
    // eight fields; negative int32 axes are sign-extended to ten-byte varints.
    const std::vector<std::uint8_t> full_state={
        0x0a,0x1e,0x08,0x03,0x2a,0x1a,
        0x08,0x80,0x20,0x10,0x7f,0x18,0x00,
        0x20,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0x01,
        0x28,0x00,0x30,0x00,0x38,0x00,0x40,0x00};
    const auto state=SimpleCmd::Parse(input::EncodeXInputStateV2(0,4096,127,0,-1,0,0,0));
    assert(state && state->cmd=="CtrlMsgV2" && state->data==full_state);
    assert(input::EncodeXInputStateV2(4,0,0,0,0,0,0,0).empty());
    std::cout<<"PASS Endfield input: mouse coordinates and virtual Xbox connection wire bytes\n";
}
