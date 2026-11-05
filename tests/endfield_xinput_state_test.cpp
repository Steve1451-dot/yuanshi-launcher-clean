#include "endfield/xinput_state.hpp"
#include <cassert>
#include <vector>

int main() {
    using namespace cloudgenshin::endfield::input;
    DeliveredXInput cache;
    cloudgenshin::input::GamepadState pad;
    std::vector<std::vector<std::uint8_t>> messages;
    const auto send=[&](const auto& frame){messages.push_back(frame);return true;};
    assert(SendDeltaXInput(cache,pad,send) && messages.size()==7);
    messages.clear();assert(SendDeltaXInput(cache,pad,send) && messages.empty());
    // Slow movement must accumulate relative to the last accepted frame.
    pad.left_thumb_x=100;assert(SendDeltaXInput(cache,pad,send));
    assert(messages.size()==1 && *cache.values[3]==100);messages.clear();
    for(int x=200;x<=600;x+=100) {
        pad.left_thumb_x=x;assert(SendDeltaXInput(cache,pad,send));
        assert(messages.empty() && *cache.values[3]==100);
    }
    pad.left_thumb_x=700;assert(SendDeltaXInput(cache,pad,send));
    assert(messages.size()==1 && *cache.values[3]==700);messages.clear();
    pad.left_thumb_x=400;assert(SendDeltaXInput(cache,pad,send));assert(messages.empty());
    pad.left_thumb_x=0;assert(SendDeltaXInput(cache,pad,send));
    assert(messages.size()==1 && messages[0]==EncodeXInputAxis(0,kXInputThumbLeftX,0));
    messages.clear();pad.right_thumb_y=-200;
    assert(SendDeltaXInput(cache,pad,send) && *cache.values[6]==-200);
    messages.clear();pad.right_thumb_y=0;
    assert(SendDeltaXInput(cache,pad,send) && messages.size()==1 && *cache.values[6]==0);
    // Partial failures retry only unsent controls; a failed release is retained.
    messages.clear();pad.buttons=kXButtonA;pad.left_trigger=255;
    const auto button=EncodeXInputButtons(0,kXButtonA);
    assert(!SendDeltaXInput(cache,pad,[&](const auto& frame){return frame==button ? false : send(frame);}));
    assert(*cache.values[0]==0 && *cache.values[1]==255 && messages.size()==1);
    messages.clear();assert(SendDeltaXInput(cache,pad,send));
    assert(messages.size()==1 && messages[0]==button);
    pad.buttons=0;
    assert(!SendDeltaXInput(cache,pad,[](const auto&){return false;}));
    assert(*cache.values[0]==kXButtonA);
    assert(SendDeltaXInput(cache,pad,send) && *cache.values[0]==0);

    DeliveredXInput connection;
    messages.clear();
    assert(!SendConnectedXInput(connection,true,pad,[&](const auto& frame) {
        messages.push_back(frame);return false;
    }));
    assert(!connection.connected && messages.size()==1 && messages[0]==EncodeXInputConnection(true));
    for(const auto& value:connection.values) assert(!value); // No controls before attach is accepted.
    messages.clear();
    assert(SendConnectedXInput(connection,true,pad,send) && messages.size()==8);
    assert(messages.front()==EncodeXInputConnection(true) && connection.connected==true);
    messages.clear();
    assert(SendConnectedXInput(connection,true,pad,send) && messages.empty());
    assert(!SendConnectedXInput(connection,false,pad,[](const auto&){return false;}));
    assert(connection.connected==true); // Failed detach is retried, not cached.
    assert(SendConnectedXInput(connection,false,pad,send) && messages.size()==1);
    assert(messages.front()==EncodeXInputConnection(false) && connection.connected==false);
    messages.clear();
    assert(SendConnectedXInput(connection,false,pad,send) && messages.empty());
    assert(SendConnectedXInput(connection,true,pad,send) && messages.size()==8);
    assert(messages.front()==EncodeXInputConnection(true)); // Full snapshot after reattach.
    messages.clear();
    assert(SendConnectedXInput(connection,true,pad,send,2) && messages.size()==1);
    assert(messages[0]==EncodeXInputStateV2(0,pad.buttons,pad.left_trigger,pad.right_trigger,
        pad.left_thumb_x,pad.left_thumb_y,pad.right_thumb_x,pad.right_thumb_y));
    messages.clear();
    assert(SendConnectedXInput(connection,true,pad,send,2) && messages.empty());
    pad.buttons=kXButtonA;
    assert(!SendConnectedXInput(connection,true,pad,[](const auto&){return false;},2));
    assert(connection.values[0]==0);
    assert(SendConnectedXInput(connection,true,pad,send,2) && messages.size()==1);
    assert(connection.values[0]==kXButtonA);
    messages.clear();pad.buttons=0;
    assert(!SendConnectedXInput(connection,true,pad,[](const auto&){return false;},2));
    assert(connection.values[0]==kXButtonA);
    assert(SendConnectedXInput(connection,true,pad,send,2) && messages.size()==1);
    messages.clear();
    assert(SendConnectedXInput(connection,true,pad,send,1) && messages.size()==7);

    // Endfield uses the shared "Face buttons" setting. The default keeps
    // physical positions; "Switch" matches printed labels instead.
    cloudgenshin::input::ControllerFrame physical_frame;
    physical_frame.connected=true;physical_frame.generation=11;
    physical_frame.state.b=true;
    DeliveredXInput physical;
    cloudgenshin::input::ControllerNeutralHandoff physical_handoff;
    messages.clear();
    assert(SendCapturedXInput(physical,physical_handoff,physical_frame,send,2));
    assert(messages.size()==2 && messages[0]==EncodeXInputConnection(true) &&
        messages[1]==EncodeXInputStateV2(0,0,0,0,0,0,0,0));
    messages.clear();
    assert(SendCapturedXInput(physical,physical_handoff,physical_frame,send,2));
    assert(messages.size()==1 &&
        messages[0]==EncodeXInputStateV2(0,kXButtonA,0,0,0,0,0,0));

    cloudgenshin::input::ControllerFrame switch_frame;
    switch_frame.connected=true;switch_frame.generation=12;
    switch_frame.state.a=true;
    DeliveredXInput labels;
    cloudgenshin::input::ControllerNeutralHandoff labels_handoff;
    messages.clear();
    assert(SendCapturedXInput(labels,labels_handoff,switch_frame,send,2,"Switch"));
    assert(messages.size()==2 && messages[0]==EncodeXInputConnection(true) &&
        messages[1]==EncodeXInputStateV2(0,0,0,0,0,0,0,0));
    messages.clear();
    assert(SendCapturedXInput(labels,labels_handoff,switch_frame,send,2,"Switch"));
    assert(messages.size()==1 &&
        messages[0]==EncodeXInputStateV2(0,kXButtonA,0,0,0,0,0,0));

    // The same physical Switch A reaches XInput B under the default layout,
    // proving the setting changes the wire value and not only the help text.
    DeliveredXInput positions;
    cloudgenshin::input::ControllerNeutralHandoff positions_handoff;
    switch_frame.generation=13;
    messages.clear();
    assert(SendCapturedXInput(positions,positions_handoff,switch_frame,send,2));
    messages.clear();
    assert(SendCapturedXInput(positions,positions_handoff,switch_frame,send,2));
    assert(messages.size()==1 &&
        messages[0]==EncodeXInputStateV2(0,kXButtonB,0,0,0,0,0,0));

    // Minus reaches both real Endfield encoders as game Back (map).
    switch_frame.state={};switch_frame.state.minus=true;messages.clear();
    assert(SendCapturedXInput(positions,positions_handoff,switch_frame,send,2));
    assert(messages.size()==1 && messages[0]==EncodeXInputStateV2(0,0x20,0,0,0,0,0,0));
    switch_frame.state={};messages.clear();
    assert(SendCapturedXInput(positions,positions_handoff,switch_frame,send,2));
    assert(messages.size()==1 && messages[0]==EncodeXInputStateV2(0,0,0,0,0,0,0,0));
    switch_frame.state.a=true;
    // V1 delta delivery follows the same layout decision.
    DeliveredXInput labels_v1;
    cloudgenshin::input::ControllerNeutralHandoff labels_v1_handoff;
    switch_frame.generation=14;
    messages.clear();
    assert(SendCapturedXInput(labels_v1,labels_v1_handoff,switch_frame,send,1,"Switch"));
    assert(messages.size()==8 && messages.front()==EncodeXInputConnection(true));
    messages.clear();
    assert(SendCapturedXInput(labels_v1,labels_v1_handoff,switch_frame,send,1,"Switch"));
    assert(messages.size()==1 && messages[0]==EncodeXInputButtons(0,kXButtonA));
    switch_frame.state={};switch_frame.state.minus=true;messages.clear();
    assert(SendCapturedXInput(labels_v1,labels_v1_handoff,switch_frame,send,1,"Switch"));
    assert(messages.size()==1 && messages[0]==EncodeXInputButtons(0,0x20));
}
