#include "endfield_stream_view.hpp"
#include "endfield/entry_probe_policy.hpp"
#include "app_paths.hpp"
#include "mihoyo/stream_health_draw.hpp"
#include "mihoyo/controller_mapping_draw.hpp"
#include "mihoyo/compact_stream_menu_draw.hpp"
#include <algorithm>
#include <cstdio>

namespace cloudgenshin::endfield {
namespace {
namespace overlay=cloudgenshin::overlay;
}

void EndfieldStreamView::arm_input_release() {
    const auto neutral=input_capture_.arm();
    std::lock_guard lock(mutex_);input_=neutral;motion_input_={};
}
void EndfieldStreamView::show_quality_menu(bool visible) {
    if(visible && exit_requested_) return;
    if(visible && diagnostics_visible_) show_diagnostics(false);
    if(quality_menu_open_==visible) return;
    quality_menu_open_=visible;mapping_editor_.close();confirm_exit_=false;menu_feedback_.clear();
    menu_tap_.cancel();floating_ball_.cancel();arm_input_release();
    if(visible) {
        stream_menu_.reset();quality_menu_index_=0;
        brls::Application::giveFocus(this);
    }
    progress_(visible ? "QUALITY_MENU_OPENED" : "QUALITY_MENU_CLOSED",0,0);
}
void EndfieldStreamView::persist_stream_settings() {
    const int tier=accepted_tier_.value_or(persisted_tier_);
    const int fps=accepted_fps_.value_or(persisted_fps_);
    if(!ValidBitrateTier(tier) || !ValidStreamFps(fps) ||
       (tier==persisted_tier_ && fps==persisted_fps_)) return;
    if(!SaveStreamSettings(opennow::AppHomePath()+"/endfield-stream-settings.txt",tier,fps)) {
        progress_("ENDFIELD_STREAM_SETTINGS_SAVE_FAILED",tier*100+fps,0);return;
    }
    persisted_tier_=tier;persisted_fps_=fps;
    progress_("ENDFIELD_STREAM_SETTINGS_SAVED",tier*100+fps,0);
}
void EndfieldStreamView::activate_quality_row(int row) {
    if(row<0 || row>=stream_menu_.count()) return;
    quality_menu_index_=row;
    const auto selection=stream_menu_.select(row,bitrate_request_.snapshot().value(),fps_request_.snapshot().value());
    if(selection.navigation) {
        quality_menu_index_=selection.focus;confirm_exit_=false;menu_feedback_.clear();
        menu_tap_.cancel();arm_input_release();return;
    }
    row=selection.command;
    if(row!=10) confirm_exit_=false;
    if(row<=4) {
        if(!settings_ready_ || finished_ || exit_requested_) {
            menu_feedback_="Settings unavailable until game auth";return;
        }
        if(row<=2) {bitrate_request_.request(row);progress_("ENDFIELD_BITRATE_REQUESTED",kStreamBitrates[row].maximum,0);}
        else {const int fps=row==3 ? 30 : 60;fps_request_.request(fps);progress_("STREAM_FPS_REQUESTED",fps,0);}
        menu_feedback_="Request queued; check actual stream.";
    } else if(row==5) stats_visible_=!stats_visible_;
    else if(row==6) {
        ui::chinese=!ui::chinese;texts_.refresh();
        opennow::PrepareAppStorage();
        progress_(ui::SaveLanguage(opennow::AppHomePath()+"/cloudgenshin-language.txt") ? "LANGUAGE_SAVED" : "LANGUAGE_SAVE_FAILED",0,0);
        diagnostics_refresh_={};
    } else if(row==7) show_diagnostics(true);
    else if(row==8) {
        mapping_editor_.open("endfield",controller_mapping_);arm_input_release();
        progress_("CONTROLLER_MAPPING_OPENED",3,0);
    }
    else if(row==9) {
        if(!input_channel_ready_ || media_.decoded()==0 || finished_ ||
           !AllowTitleProbe(static_cast<GameBridge::Request>(game_lifecycle_.load()))) {
            menu_feedback_="Continue needs video and control ready";return;
        }
        continue_requested_=true;show_quality_menu(false);
    } else if(row==10) {
        if(!confirm_exit_) {confirm_exit_=true;menu_feedback_="Select stop again to confirm. B cancels.";}
        else request_exit();
    } else if(row==11) {
        const bool saved=decoder_selection_.toggle();
        menu_feedback_=saved ? "Decoder saved; stop and start stream to apply" : "SD failed / photograph log";
        progress_(saved ? "VIDEO_DECODER_PREFERENCE_SAVED" : "VIDEO_DECODER_PREFERENCE_SAVE_FAILED",decoder_selection_.preferred()?1:0,0);
    }
}
void EndfieldStreamView::update_overlay_input(const cloudgenshin::input::SwitchControllerState& state,
    overlay::Rect bounds,bool touch_down,int finger,overlay::Point point) {
    const bool was_open=quality_menu_open_;
    if(mapping_editor_.is_open() && !diagnostics_visible_ && !exit_requested_ && !finished_) {
        const auto action=mapping_editor_.update(state,bounds,touch_down,finger,point);
        if(action==cloudgenshin::input::ControllerMappingEditor::Action::Save) {
            opennow::PrepareAppStorage();
            const bool saved=cloudgenshin::input::SaveControllerMapping(opennow::AppHomePath(),"endfield",mapping_editor_.candidate());
            if(saved) {std::lock_guard lock(mutex_);controller_mapping_=mapping_editor_.candidate();}
            mapping_editor_.saved(saved);arm_input_release();
            progress_(saved?"CONTROLLER_MAPPING_SAVED":"CONTROLLER_MAPPING_SAVE_FAILED",3,0);
        } else if(action==cloudgenshin::input::ControllerMappingEditor::Action::Close) arm_input_release();
        quality_menu_up_=state.dpad_up;quality_menu_down_=state.dpad_down;
        quality_menu_a_=state.a;quality_menu_b_=state.b;
        return;
    }
    bool touch_committed=false;
    if(diagnostics_visible_ || exit_requested_ || finished_) {
        floating_ball_.update(touch_down,finger,point,bounds);floating_ball_.cancel();menu_tap_.cancel();
    } else if(quality_menu_open_) {
        const overlay::CompactStreamMenuLayout layout(bounds,stream_menu_.count());
        const int target=layout.close.contains(point) || !layout.panel.contains(point) ? -2 : layout.hit_row(point);
        if(const auto tap=menu_tap_.update(touch_down,finger,target)) {
            touch_committed=true;
            if(*tap==-2) show_quality_menu(false);else activate_quality_row(*tap);
        }
    } else {
        if(floating_ball_.update(touch_down,finger,point,bounds)) show_quality_menu(true);
        if(floating_ball_.captured() && !ball_was_captured_) arm_input_release();
        ball_was_captured_=floating_ball_.captured();menu_tap_.update(touch_down,finger,-1);
    }
    // Touch is the ONLY opening gesture: L/R and their combinations remain
    // ordinary gameplay input. Once open, navigation matches the Genshin menu.
    if(was_open && quality_menu_open_ && !touch_committed) {
        if((state.dpad_up && !quality_menu_up_) || (state.dpad_down && !quality_menu_down_)) {
            confirm_exit_=false;menu_feedback_.clear();
        }
        if(state.dpad_up && !quality_menu_up_) quality_menu_index_=(quality_menu_index_+stream_menu_.count()-1)%stream_menu_.count();
        if(state.dpad_down && !quality_menu_down_) quality_menu_index_=(quality_menu_index_+1)%stream_menu_.count();
        if(state.b && !quality_menu_b_) {
            if(confirm_exit_) {confirm_exit_=false;menu_feedback_.clear();}
            else if(const auto focus=stream_menu_.back()) {
                quality_menu_index_=*focus;menu_feedback_.clear();menu_tap_.cancel();arm_input_release();
            } else show_quality_menu(false);
        } else if(state.a && !quality_menu_a_) activate_quality_row(quality_menu_index_);
    }
    quality_menu_up_=state.dpad_up;quality_menu_down_=state.dpad_down;
    quality_menu_a_=state.a;quality_menu_b_=state.b;
}

void EndfieldStreamView::draw_overlay(NVGcontext* vg,overlay::Rect bounds) {
    if(diagnostics_visible_ || exit_requested_) return;
    nvgSave(vg);nvgIntersectScissor(vg,bounds.x,bounds.y,bounds.w,bounds.h);
    nvgFontFaceId(vg,brls::Application::getFont(ui::chinese ? brls::FONT_CHINESE_SIMPLIFIED : brls::FONT_REGULAR));
    if(mapping_editor_.is_open()) {
        overlay::DrawControllerMapping(vg,bounds,mapping_editor_,ui::Text("Endfield Cloud"),controller_layout_);
        nvgRestore(vg);return;
    }
    const auto cyan=nvgRGB(90,225,235);
    std::string resolution;
    if(stats_visible_ || quality_menu_open_) {
        resolution=frame_metadata_seen_ ? std::to_string(frame_width_.load())+" x "+std::to_string(frame_height_.load()) : "-- x --";
    }
    if(stats_visible_ && !quality_menu_open_) {
        overlay::DrawStreamHealth(vg,bounds.x+20,bounds.y+18,resolution,hardware_decoder_,stream_health_.window());
    }
    if(!quality_menu_open_) {
        const auto c=floating_ball_.centre(bounds);
        nvgBeginPath(vg);nvgCircle(vg,c.x,c.y,29);nvgFillColor(vg,nvgRGBA(12,28,44,215));nvgFill(vg);
        nvgStrokeColor(vg,cyan);nvgStrokeWidth(vg,2);nvgStroke(vg);
        for(int i=0;i<3;++i) {
            const float yy=c.y-10+i*10,xx=c.x+(i==1 ? 7 : -6);
            nvgBeginPath(vg);nvgMoveTo(vg,c.x-14,yy);nvgLineTo(vg,c.x+14,yy);nvgStrokeWidth(vg,2);nvgStrokeColor(vg,cyan);nvgStroke(vg);
            nvgBeginPath(vg);nvgCircle(vg,xx,yy,3);nvgFillColor(vg,cyan);nvgFill(vg);
        }
        nvgRestore(vg);return;
    }
    overlay::CompactStreamMenuStatus status;
    status.resolution=resolution;status.feedback=menu_feedback_;
    status.decoded_fps=stream_rates_.fps();status.mbps=stream_rates_.mbps();
    status.hardware=hardware_decoder_;status.settings_ready=settings_ready_ && !finished_;
    status.control_ready=input_channel_ready_ && !finished_;
    status.continue_ready=status.control_ready && media_.decoded()>0 &&
        AllowTitleProbe(static_cast<GameBridge::Request>(game_lifecycle_.load()));
    status.hud=stats_visible_;status.hardware_preferred=decoder_selection_.preferred();status.confirm_exit=confirm_exit_;
    status.quality=bitrate_request_.snapshot();status.fps=fps_request_.snapshot();
    overlay::DrawCompactStreamMenu(vg,bounds,stream_menu_,quality_menu_index_,status);
    nvgRestore(vg);
}
} // namespace cloudgenshin::endfield
