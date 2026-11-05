#include "mihoyo/gameplay_button_policy.hpp"
#include "mihoyo/stream_input_policy.hpp"
#include <cassert>
#include <iostream>

int main() {
    using namespace cloudgenshin::input;
    // Exhaust all sixteen button states, including simultaneous presses,
    // against actual XInput encoding; all non-face fields must be unchanged.
    for(unsigned mask=0;mask<65536;++mask) {
        SwitchControllerState raw;
        bool* fields[]={&raw.a,&raw.b,&raw.x,&raw.y,&raw.l,&raw.r,&raw.zl,&raw.zr,
            &raw.dpad_up,&raw.dpad_down,&raw.dpad_left,&raw.dpad_right,
            &raw.plus,&raw.minus,&raw.stick_left,&raw.stick_right};
        for(unsigned bit=0;bit<16;++bit) *fields[bit]=(mask&(1u<<bit))!=0;
        raw.left_x=1234;raw.left_y=-2345;raw.right_x=32767;raw.right_y=-32767;
        const auto original=ToGamepadState(raw,123);
        for(const auto app:{"4","8","12","endfield","","04"}) {
            const auto mapped=GameplayFaceButtons(app,raw);
            const auto packet=ToGamepadState(mapped,123);
            std::uint32_t expected=original.buttons;
            if(std::string_view(app)=="4") {
                expected=(expected&~0xf000u)|((original.buttons&0x1000u)<<1)|
                    ((original.buttons&0x2000u)>>1)|((original.buttons&0x4000u)<<1)|((original.buttons&0x8000u)>>1);
            }
            assert(packet.buttons==expected);
            assert(packet.left_trigger==original.left_trigger && packet.right_trigger==original.right_trigger);
            assert(packet.left_thumb_x==original.left_thumb_x && packet.left_thumb_y==original.left_thumb_y);
            assert(packet.right_thumb_x==original.right_thumb_x && packet.right_thumb_y==original.right_thumb_y);
            assert(packet.vendor_id==original.vendor_id && packet.product_id==original.product_id && packet.rtt_trace_send_time==123);
            // Remapping does not alter the input copy used by local UI.
            assert(raw.a==((mask&1)!=0) && raw.b==((mask&2)!=0) &&
                   raw.x==((mask&4)!=0) && raw.y==((mask&8)!=0));
            for(const bool confirm:{false,true}) for(const bool automatic:{false,true}) {
                const auto frame=ComposeGamepadFrame(mapped,confirm,automatic);
                if(confirm || automatic) {
                    const auto synthetic=ToGamepadState(frame,0);
                    assert(synthetic.buttons==0x1000 && synthetic.left_trigger==0 && synthetic.right_trigger==0);
                    assert(synthetic.left_thumb_x==0 && synthetic.right_thumb_y==0);
                }
            }
        }
    }
    SwitchControllerState face;
    face.a=true;assert(ToGamepadState(GameplayFaceButtons("4",face),0).buttons==0x1000);
    face={};face.b=true;assert(ToGamepadState(GameplayFaceButtons("4",face),0).buttons==0x2000);
    face={};face.x=true;assert(ToGamepadState(GameplayFaceButtons("4",face),0).buttons==0x4000);
    face={};face.y=true;assert(ToGamepadState(GameplayFaceButtons("4",face),0).buttons==0x8000);
    assert(ControlsReleased(GameplayFaceButtons("4",{})));
    std::cout<<"PASS Genshin-only AB/XY swap: 65536 button states, six profiles, actual wire bits, synthetic entry unchanged\n";
}
