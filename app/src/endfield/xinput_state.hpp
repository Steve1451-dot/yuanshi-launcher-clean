#pragma once
#include "endfield/wl_input.hpp"
#include "mihoyo/input_packet.hpp"
#include "mihoyo/controller_capture_policy.hpp"
#include "mihoyo/controller_source_policy.hpp"
#include <array>
#include <optional>
#include <string_view>

namespace cloudgenshin::endfield::input {

// Last locally accepted value of each control, not the last sampled value.
// Keeping the two separate lets small stick steps accumulate to the threshold.
struct DeliveredXInput {
    std::array<std::optional<std::int32_t>,7> values{};
    std::optional<bool> connected;
    int input_version=1;
};

inline std::array<std::int32_t,7> XInputValues(const cloudgenshin::input::GamepadState& pad) {
    return {static_cast<std::int32_t>(pad.buttons),static_cast<std::int32_t>(pad.left_trigger),
        static_cast<std::int32_t>(pad.right_trigger),
        pad.left_thumb_x,pad.left_thumb_y,pad.right_thumb_x,pad.right_thumb_y};
}
inline bool XInputValueChanged(std::size_t i,const std::optional<std::int32_t>& previous,
                              std::int32_t next) {
    if(previous && *previous==next) return false;
    // Small motion may accumulate, but crossing zero is a direction change.
    // Suppressing +100 -> -100 would leave the remote stick pointing right.
    if(i>=3 && previous && next!=0 && *previous!=0 && (next>0)==(*previous>0)) {
        const auto difference=static_cast<std::int64_t>(next)-*previous;
        if(difference>-512 && difference<512) return false;
    }
    return true;
}

template<class Send>
bool SendDeltaXInput(DeliveredXInput& delivered,
                    const cloudgenshin::input::GamepadState& pad,Send&& send) {
    const auto next=XInputValues(pad);
    bool ok=true;
    for(std::size_t i=0;i<next.size();++i) {
        auto& previous=delivered.values[i];
        // An axis returning to zero must always reach the pod. Starting from
        // neutral is also an edge, even below the usual ~1.6% delta threshold.
        if(!XInputValueChanged(i,previous,next[i])) continue;
        const auto message=i==0 ? EncodeXInputButtons(0,next[i])
            : i<3 ? EncodeXInputTrigger(0,i==1,next[i])
            : EncodeXInputAxis(0,kXInputThumbLeftX+static_cast<int>(i-3),next[i]);
        if(send(message)) previous=next[i];
        else ok=false; // Retry this field; do not roll back successful fields.
    }
    return ok;
}

template<class Send>
bool SendConnectedXInput(DeliveredXInput& delivered,bool connected,
                        const cloudgenshin::input::GamepadState& pad,Send&& send,int input_version=1) {
    if(!delivered.connected || *delivered.connected!=connected) {
        if(!send(EncodeXInputConnection(connected))) return false;
        delivered.connected=connected;
        delivered.values={}; // A reattached virtual pad needs a full snapshot.
    }
    if(!connected) return true;
    if(delivered.input_version!=input_version) {
        delivered.input_version=input_version;
        delivered.values={};
    }
    if(input_version>=2) {
        const auto next=XInputValues(pad);
        bool changed=false;
        for(std::size_t i=0;i<next.size();++i)
            changed=changed || XInputValueChanged(i,delivered.values[i],next[i]);
        if(!changed) return true;
        if(!send(EncodeXInputStateV2(0,next[0],next[1],next[2],next[3],next[4],next[5],next[6]))) return false;
        for(std::size_t i=0;i<next.size();++i) delivered.values[i]=next[i];
        return true;
    }
    return SendDeltaXInput(delivered,pad,send);
}

// Never admit the new source/capture's controls before the old controls have
// been released. Cache only locally accepted writes; partial V1 neutral
// reports keep retrying their unsent fields, while V2 is one full snapshot.
// controller_layout is the shared "Face buttons" choice and must stay fixed
// for one stream's delivery cache.
template<class Send>
bool SendCapturedXInput(DeliveredXInput& delivered,
    cloudgenshin::input::ControllerNeutralHandoff& handoff,
    const cloudgenshin::input::ControllerFrame& frame,Send&& send,int input_version=1,
    std::string_view controller_layout="Xbox") {
    if(handoff.pending(frame.generation)) {
        if((delivered.connected.value_or(false) || frame.connected) &&
           !SendConnectedXInput(delivered,true,{},send,input_version)) return false;
        if(!frame.connected && !SendConnectedXInput(delivered,false,{},send,input_version)) return false;
        handoff.complete(frame.generation,true);
        return true; // First frame of each generation is neutral, even on attach.
    }
    if(!frame.connected && delivered.connected.value_or(false) &&
       !SendConnectedXInput(delivered,true,{},send,input_version)) return false;
    return SendConnectedXInput(delivered,frame.connected,
        cloudgenshin::input::ToGamepadState(frame.state,0,
            cloudgenshin::input::kXboxVendorId,cloudgenshin::input::kXboxProductId,
            controller_layout),send,input_version);
}

} // namespace cloudgenshin::endfield::input
