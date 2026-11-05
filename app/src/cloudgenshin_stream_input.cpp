#include "cloudgenshin_stream_view.hpp"
#include "mihoyo/rtc_session.hpp"

#include <algorithm>

double CloudGenshinStreamView::normalized_x(double pixel_x) const {
    const double width=static_cast<double>(frame_width_.load());
    return width>0.0?std::clamp(pixel_x/width,0.0,1.0):0.0;
}

double CloudGenshinStreamView::normalized_y(double pixel_y) const {
    const double height=static_cast<double>(frame_height_.load());
    return height>0.0?std::clamp(pixel_y/height,0.0,1.0):0.0;
}

bool CloudGenshinStreamView::send_mouse_button_at(cloudgenshin::rtc::Session& session,
    bool down,double pixel_x,double pixel_y) {
    return session.send_mouse_button(cloudgenshin::input::kMouseButtonLeft,down,
                                     normalized_x(pixel_x),normalized_y(pixel_y));
}

bool CloudGenshinStreamView::send_launcher_pointer(cloudgenshin::rtc::Session& session,
    bool down,double x,double y) {
    using namespace cloudgenshin::input;
    if(launcher_touch_) return session.send_touch({{0,down?kTouchActionDown:kTouchActionUp,x,y,true}});
    if(down && !move_mouse_to(session,x*frame_width_.load(),y*frame_height_.load())) return false;
    return remote_keys_.mouse_left.apply(down,[&](bool held) {
        return session.send_mouse_button(kMouseButtonLeft,held,x,y);
    },true);
}

void CloudGenshinStreamView::send_keyboard_mouse(cloudgenshin::rtc::Session& session,
    const cloudgenshin::input::SwitchControllerState& state) {
    using namespace cloudgenshin;
    if(!session.input_ready()) return;
    constexpr std::int32_t kStickDeadzone=11000;
    constexpr double kMouseSensitivity=22.0;
    auto& mapped=remote_keys_;
    bool sent=false;
    const auto key=[&](bool down,input::DeliveredButton& previous,std::uint32_t code) {
        if(down!=previous.held() && previous.apply(down,[&](bool held){return session.send_keyboard(code,held);})) sent=true;
    };
    key(state.dpad_left,mapped.key1,input::kVkKey1);
    key(state.dpad_up,mapped.key2,input::kVkKey2);
    key(state.dpad_right,mapped.key3,input::kVkKey3);
    key(state.dpad_down,mapped.key4,input::kVkKey4);
    key(state.left_x<-kStickDeadzone,mapped.a,input::kVkKeyA);
    key(state.left_x>kStickDeadzone,mapped.d,input::kVkKeyD);
    key(state.left_y>kStickDeadzone,mapped.w,input::kVkKeyW);
    key(state.left_y<-kStickDeadzone,mapped.s,input::kVkKeyS);
    key(state.b,mapped.space,input::kVkSpace);
    key(state.x,mapped.e,input::kVkKeyE);key(state.y,mapped.q,input::kVkKeyQ);
    key(state.l,mapped.shift,input::kVkShift);key(state.r,mapped.f,input::kVkKeyF);
    key(state.plus,mapped.escape,input::kVkEscape);
    key(state.minus,mapped.m,input::kVkKeyM);
    const auto button=[&](bool down,input::DeliveredButton& previous,std::uint32_t code) {
        const double nx=mouse_position_known_?normalized_x(mouse_x_):0.5;
        const double ny=mouse_position_known_?normalized_y(mouse_y_):0.5;
        if(down!=previous.held() && previous.apply(down,[&](bool held){return session.send_mouse_button(code,held,nx,ny);})) sent=true;
    };
    button(state.a||state.zr,mapped.mouse_left,input::kMouseButtonLeft);
    button(state.zl,mapped.mouse_right,input::kMouseButtonRight);
    const double mx=static_cast<double>(state.right_x)/32767.0;
    const double my=static_cast<double>(-state.right_y)/32767.0;
    if(mx*mx+my*my>0.02) {
        const double dx=mx*kMouseSensitivity,dy=my*kMouseSensitivity;
        double cursor_x=0.5,cursor_y=0.5;
        if(mouse_position_known_) {
            mouse_x_+=dx;mouse_y_+=dy;
            const double width=frame_width_.load(),height=frame_height_.load();
            mouse_x_=std::clamp(mouse_x_,0.0,width>0.0?width:0.0);
            mouse_y_=std::clamp(mouse_y_,0.0,height>0.0?height:0.0);
            cursor_x=normalized_x(mouse_x_);cursor_y=normalized_y(mouse_y_);
        }
        if(session.send_mouse_move(dx,dy,cursor_x,cursor_y)) sent=true;
    }
    if(sent && !remote_input_reported_) {
        remote_input_reported_=true;progress_("KEYBOARD_MOUSE_INPUT_ACTIVE",0,0);
    }
}

bool CloudGenshinStreamView::move_mouse_to(cloudgenshin::rtc::Session& session,double x,double y) {
    const double delta_x=mouse_position_known_?x-mouse_x_:0.0;
    const double delta_y=mouse_position_known_?y-mouse_y_:0.0;
    // Official web client: absolute cursor in [0,1] against the video rect,
    // relative movement in screen pixels. Zero is omitted by proto3.
    if(!session.send_mouse_move(delta_x,delta_y,normalized_x(x),normalized_y(y))) return false;
    mouse_x_=x;mouse_y_=y;mouse_position_known_=true;
    return true;
}

void CloudGenshinStreamView::calibrate_mouse(cloudgenshin::rtc::Session& session) {
    // Park the pod cursor at the normalised origin. The official page never
    // sends the synthetic -4096 relative jump earlier builds used; with
    // normalised absolute coordinates the origin is directly addressable.
    if(session.send_mouse_move(0.0,0.0,0.0,0.0)) {
        mouse_x_=0.0;mouse_y_=0.0;mouse_position_known_=true;
    }
    mouse_calibrated_=true;
    progress_("AUTOTEST_ENTRY_CURSOR_CALIBRATED",0,0);
}

void CloudGenshinStreamView::process_continue_click(cloudgenshin::rtc::Session& session,std::int64_t now_ms) {
    using namespace cloudgenshin::input;
    if(!session.input_ready()) return;
    // The manual continue click targets the video centre in every step.
    const double kCenterX=0.5,kCenterY=launcher_entered_?0.5:0.947;
    const double center_x=frame_width_.load()*kCenterX;
    const double center_y=frame_height_.load()*kCenterY;
    const auto release=[&] {
        return send_launcher_pointer(session,false,kCenterX,kCenterY);
    };
    if(!continue_click_.active() && continue_release_pending_) {
        if(now_ms<continue_release_retry_at_) return;
        if(release()) continue_release_pending_=false;
        continue_release_retry_at_=now_ms+100;
        return; // Never start another click before the previous release is sent.
    }
    if(!continue_click_.active() && media_.decoded()>0 && continue_requested_.exchange(false)) {
        send_keyboard_mouse(session,{});
        continue_click_.request(now_ms);
    }
    const auto action=continue_click_.next(now_ms);
    if(action==ManualContinueClick::Action::None) return;
    if(action==ManualContinueClick::Action::Failed) {
        progress_("INPUT_CONTINUE_SEND_FAILED",0,0);return;
    }
    bool sent=false;
    if(action==ManualContinueClick::Action::Move) {
        sent=launcher_touch_ || (remote_keys_.mouse_left.apply(false,[&](bool down){return session.send_mouse_button(kMouseButtonLeft,down,kCenterX,kCenterY);}) &&
            move_mouse_to(session,center_x,center_y));
    } else if(action==ManualContinueClick::Action::Down) {
        sent=send_launcher_pointer(session,true,kCenterX,kCenterY);
        if(sent) continue_release_pending_=true;
    } else if(action==ManualContinueClick::Action::Up) {
        sent=release();if(sent) continue_release_pending_=false;
    }
    if(continue_click_.sent(sent,now_ms)) progress_("INPUT_WAKE_CLICK_SENT",0,0);
}
