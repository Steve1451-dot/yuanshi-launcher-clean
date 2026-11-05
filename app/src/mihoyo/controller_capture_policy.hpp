#pragma once

#include "mihoyo/stream_input_policy.hpp"

namespace cloudgenshin::input {

// Standalone Minus/Plus belong to the game. Once the exit chord is captured,
// keep BOTH local until both are up; a partial release must not open a remote
// map/menu. Observe raw input even while a panel suppresses gameplay.
class ControllerSystemButtons {
public:
    void observe(const SwitchControllerState& raw) {
        if(raw.plus && raw.minus) chord_=true;
        if(!raw.plus && !raw.minus) chord_=false;
    }
    void filter(SwitchControllerState& state) const {
        if(chord_) state.plus=state.minus=false;
    }
    void reset() {chord_=false;}
private:
    bool chord_=false;
};

// Separate physical shortcut: hold both stick clicks for 800ms. Single Minus
// is never consumed here. The shared hold gate captures until both sticks are
// released, so closing diagnostics cannot leak a leftover L3/R3 press.
class DiagnosticsChord {
public:
    QualityMenuChord::Decision update(const SwitchControllerState& raw,std::int64_t now,bool enabled=true) {
        if(!enabled) {chord_={};return {};}
        return chord_.update(raw.stick_left,raw.stick_right,now);
    }
private:
    QualityMenuChord chord_;
};

struct ControllerFrame {
    SwitchControllerState state;
    bool connected=false;
    unsigned generation=1;
};

// UI-thread capture. Publish the entire frame under one lock. A generation
// survives a panel opening/closing or a source disappearing between worker
// polls, so even a very short capture still requires a delivered neutral.
class ControllerCapture {
public:
    bool motion_allowed() const {return frame_.connected && !captured_ && !release_pending_;}
    ControllerFrame arm() {
        ++frame_.generation;
        frame_.state={};release_pending_=true;
        return frame_;
    }
    ControllerFrame update(const SwitchControllerState& raw,int source,bool captured) {
        if(source!=source_) {
            source_=source;arm();system_buttons_.reset();
        }
        if(captured!=captured_) {captured_=captured;arm();}
        frame_.connected=source>=0;
        system_buttons_.observe(raw);
        frame_.state=raw;
        if(release_pending_) {
            if(ControlsReleased(raw)) release_pending_=false;
            frame_.state={};
        }
        if(captured || !frame_.connected) frame_.state={};
        system_buttons_.filter(frame_.state);
        return frame_;
    }
private:
    ControllerFrame frame_;
    ControllerSystemButtons system_buttons_;
    int source_=-1;
    bool captured_=false,release_pending_=true;
};

} // namespace cloudgenshin::input
