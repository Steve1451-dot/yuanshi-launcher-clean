#pragma once
#include "mihoyo/controller_mapping.hpp"
#include "mihoyo/stream_overlay_policy.hpp"
#include "mihoyo/controller_mapping_diagram.hpp"

namespace cloudgenshin::input {
// UI-only state machine shared by both stream views. File writes and gameplay
// publication belong to the view; a failed save never changes current config.
class ControllerMappingEditor {
public:
    enum class Action {None,Close,Save,ToggleNavigation};
    void open(std::string_view profile,const ControllerMapping& current,bool native_navigation=false) {
        profile_=profile;current_=candidate_=current;open_=true;targets_=false;
        native_navigation_=profile=="hk4e" && native_navigation;
        page_=focus_=0;armed_=confirm_reset_=false;tap_.cancel();feedback_.clear();
        pressed_={};
    }
    void close() {open_=false;tap_.cancel();}
    void suspend_input() {armed_=false;tap_.cancel();confirm_reset_=false;pressed_={};}
    bool is_open() const {return open_;}
    bool targets() const {return targets_;}
    int page() const {return page_;}
    int focus() const {return focus_;}
    unsigned source() const {return source_;}
    bool gyro_page() const {return !targets_ && page_==2;}
    bool gyro_supported() const {return profile_=="hk4e";}
    bool native_navigation() const {return native_navigation_;}
    const auto& current() const {return current_;}
    const auto& candidate() const {return candidate_;}
    const auto& pressed() const {return pressed_;} // UI highlight only, never remote input.
    std::optional<unsigned> highlighted_source() const {
        if(targets_) return source_;
        if(!gyro_page() && focus_<8) return static_cast<unsigned>(page_*8+focus_);
        return std::nullopt;
    }
    const std::string& feedback() const {return feedback_;}
    void saved(bool success) {
        if(success) {current_=candidate_;feedback_="Mapping saved for this game";}
        else feedback_="SD save failed; previous mapping kept";
    }
    Action select_source(unsigned source) {
        if(source>=kMappedButtons.size()) return Action::None;
        candidate_=current_;confirm_reset_=false;
        if(source==kFixedMinusButton) {
            targets_=false;source_=source;page_=source/8;focus_=source%8;
            feedback_="Minus is fixed to game Back / map";return Action::None;
        }
        feedback_.clear();source_=source;targets_=true;
        page_=current_.target[source_]==kDisabledButton?0:(current_.target[source_]+1)/9;
        focus_=current_.target[source_]==kDisabledButton?0:(current_.target[source_]+1)%9;
        return Action::None;
    }
    Action activate(int row) {
        if(row<0 || row>=overlay::kMenuRows) return Action::None;
        focus_=row;candidate_=current_;
        if(targets_ || row!=10) confirm_reset_=false;
        if(targets_) {
            if(row<9) {
                const unsigned choice=page_*9+row;
                if(choice>16) return Action::None;
                candidate_.target[source_]=choice==0?kDisabledButton:choice-1;
                targets_=false;page_=source_/8;focus_=source_%8;
                return Action::Save;
            }
            if(row==9 || row==10) {page_=1-page_;focus_=0;}
            if(row==11) {targets_=false;page_=source_/8;focus_=source_%8;}
        } else if(row==8 || row==9) {
            page_=(page_+(row==8?2:1))%3;focus_=0;
        } else if(row==10) {
            if(!confirm_reset_) {confirm_reset_=true;feedback_="Select restore again to confirm";return Action::None;}
            confirm_reset_=false;candidate_=DefaultControllerMapping(profile_);return Action::Save;
        }
        else if(row==11) {close();return Action::Close;}
        else if(gyro_page()) {
            if(profile_=="hk4e" && row==5) {
                native_navigation_=!native_navigation_;
                feedback_=native_navigation_?"Native navigation enabled; gyro paused":"Combat controls enabled";
                return Action::ToggleNavigation;
            }
            if(profile_!="hk4e" || row>4) return Action::None;
            if(row==0) candidate_.gyro_assist=!current_.gyro_assist;
            if(row==1) candidate_.gyro_sensitivity=current_.gyro_sensitivity==200?50:current_.gyro_sensitivity+50;
            if(row==2) candidate_.gyro_aim_only=!current_.gyro_aim_only;
            if(row==3) candidate_.gyro_invert_y=!current_.gyro_invert_y;
            if(row==4) candidate_.gyro_invert_x=!current_.gyro_invert_x;
            return Action::Save;
        } else {
            return select_source(page_*8+row);
        }
        return Action::None;
    }
    Action update(const SwitchControllerState& raw,overlay::Rect bounds,bool down,int finger,overlay::Point point) {
        if(!open_) return Action::None;
        pressed_=raw;
        if(!armed_) {
            bool held=down;for(auto member:kMappedButtons) held|=raw.*member;
            up_=raw.dpad_up;down_=raw.dpad_down;a_=raw.a;b_=raw.b;
            tap_.cancel();
            if(!held) {armed_=true;tap_.update(false,0,-1);}
            return Action::None;
        }
        const overlay::Layout layout(bounds);
        const overlay::ControllerDiagramLayout diagram(bounds);
        const int source=diagram.hit(point);
        const bool inside=layout.panel.contains(point) || (diagram.visible && diagram.panel.contains(point));
        const int target=layout.close.contains(point)||!inside ? -2 : source>=0
            ? overlay::ControllerDiagramLayout::kTargetBase+source : layout.hit_row(point);
        const auto tapped=tap_.update(down,finger,target);
        Action result=Action::None;
        if(tapped) {
            if(*tapped==-2) {close();result=Action::Close;}
            else if(*tapped>=overlay::ControllerDiagramLayout::kTargetBase)
                result=select_source(*tapped-overlay::ControllerDiagramLayout::kTargetBase);
            else result=activate(*tapped);
        }
        else if(!down) {
            if(raw.dpad_up && !up_) focus_=(focus_+overlay::kMenuRows-1)%overlay::kMenuRows;
            if(raw.dpad_down && !down_) focus_=(focus_+1)%overlay::kMenuRows;
            if(raw.b && !b_) result=activate(11);
            else if(raw.a && !a_) result=activate(focus_);
        }
        up_=raw.dpad_up;down_=raw.dpad_down;a_=raw.a;b_=raw.b;
        return result;
    }
private:
    std::string profile_,feedback_;
    ControllerMapping current_,candidate_;
    SwitchControllerState pressed_;
    bool open_=false,targets_=false,armed_=false,up_=false,down_=false,a_=false,b_=false,confirm_reset_=false;
    bool native_navigation_=false; // Session UI choice; never persisted as a new default.
    int page_=0,focus_=0;
    unsigned source_=0;
    overlay::MenuTap tap_;
};
} // namespace cloudgenshin::input
