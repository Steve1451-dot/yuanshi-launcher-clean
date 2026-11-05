#pragma once
#include <cstdint>
#include <optional>
#include <utility>
#include "mihoyo/input_packet.hpp"

namespace cloudgenshin::input {
// Desktop pods consume mouse clicks on the title overlay even when they
// expose a virtual pad. Mobile/touch terminals use the official touch stream.
inline bool UsesTouchLauncher(unsigned terminal) {
    return terminal==2 || terminal==3 || terminal==6 || terminal==7 || terminal==9;
}
inline bool ControlsReleased(const SwitchControllerState& s) {
    const auto centred=[](std::int32_t value) {return value>=-3000 && value<=3000;};
    return !(s.dpad_up || s.dpad_down || s.dpad_left || s.dpad_right || s.plus || s.minus ||
        s.stick_left || s.stick_right || s.l || s.r || s.zl || s.zr || s.a || s.b || s.x || s.y) &&
        centred(s.left_x) && centred(s.left_y) && centred(s.right_x) && centred(s.right_y);
}

// Compose the frame actually sent to the pod. Two client-side actions need to
// synthesize XInput A: the explicit UI Continue request and the automatic
// title-page entry. Both must override the physical pad state for their whole
// hold window. The continuous sender runs every ~16 ms, so a one-shot synthetic
// press that is not re-applied is cancelled by the next physical frame and the
// game sees only a stray edge (or nothing) - the earlier pad-A entry did exactly
// that. Precedence is irrelevant between the two because both press A.
inline SwitchControllerState ComposeGamepadFrame(SwitchControllerState physical,
        bool ui_confirm,bool auto_entry_hold) {
    if(ui_confirm || auto_entry_hold) {physical=SwitchControllerState{};physical.b=true;}
    return physical;
}

// The game-input release gate. After a UI panel, a floating-ball gesture or a
// Continue request the client withholds gameplay input until the physical
// controls are neutral once, so a held button cannot leak into the game.
// Regression: the gate used to also require "no touch contact". One stale
// contact then reset the state on every frame and every button press was
// silently discarded, which is why the controller appeared to do nothing.
class InputReleaseGate {
public:
    // Arm on the UI thread (panel opened, ball grabbed, Continue queued).
    void arm(std::int64_t now_ms) {pending_=true;armed_ms_=now_ms;}
    bool pending() const {return pending_;}
    // Worker side: should this frame's game input be suppressed?
    // Neutral controls or a bounded timeout both release the gate.
    bool suppress(const SwitchControllerState& raw,std::int64_t now_ms) {
        if(!pending_) return false;
        const bool neutral=ControlsReleased(raw);
        // A caller that passes a non-monotonic or zero clock must still be able
        // to release: treat a negative elapsed as "armed just now".
        const auto elapsed=now_ms>=armed_ms_ ? now_ms-armed_ms_ : 0;
        if(neutral || elapsed>kTimeoutMs) {pending_=false;return false;}
        return true;
    }
    static constexpr std::int64_t kTimeoutMs=2000;
private:
    bool pending_=true;
    std::int64_t armed_ms_=0;
};
class DeliveredButton {
public:
    bool held() const {return held_;}
    template<class Send> bool apply(bool desired,Send&& send,bool force=false) {
        if(desired==held_ && !force) return true;
        if(!std::forward<Send>(send)(desired)) return false;
        held_=desired;return true;
    }
private:
    bool held_=false;
};

class QualityMenuChord {
public:
    struct Decision {bool capture=false,open=false;};
    Decision update(bool left,bool right,std::int64_t now) {
        if(!left && !right) {captured_=false;started_.reset();return {};}
        if(left && right && !captured_) {captured_=true;started_=now;}
        if(!(left && right)) started_.reset();
        bool open=false;
        if(started_ && now-*started_>=800) {open=true;started_.reset();}
        return {captured_,open};
    }
private:
    bool captured_=false;
    std::optional<std::int64_t> started_;
};

// Only an explicit local user request starts a click. No automatic first-frame
// clicks or scene-based retries. Failed sends retry the same step, not a new click.
class ManualContinueClick {
public:
    enum class Action {None,Move,Down,Up,Failed};
    bool request(std::int64_t now) {
        if(active()) return false;
        step_=Action::Move;due_=now;deadline_=now+2000;return true;
    }
    bool active() const {return step_!=Action::None;}
    Action next(std::int64_t now) {
        if(!active()) return Action::None;
        if(now>=deadline_) {step_=Action::None;return Action::Failed;}
        return now>=due_ ? step_ : Action::None;
    }
    bool sent(bool success,std::int64_t now) {
        if(!success) {due_=now+100;return false;}
        if(step_==Action::Move) {step_=Action::Down;due_=now+60;}
        else if(step_==Action::Down) {step_=Action::Up;due_=now+90;}
        else if(step_==Action::Up) {step_=Action::None;return true;}
        return false;
    }
private:
    Action step_=Action::None;
    std::int64_t due_=0,deadline_=0;
};
}
