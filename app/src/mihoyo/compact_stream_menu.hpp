#pragma once
#include "mihoyo/stream_overlay_policy.hpp"
#include <array>
#include <string_view>

namespace cloudgenshin::overlay {
// UI-only navigation. Commands retain the existing stream handlers' IDs; a
// page change never emits a quality request, a continue click or a stop.
class CompactStreamMenu {
public:
    enum class Page {Home,Quality,FrameRate,More};
    struct Selection {int command=-1,focus=0;bool navigation=false;};
    void reset() {page_=Page::Home;}
    Page page() const {return page_;}
    int count() const {
        switch(page_) {
            case Page::Quality:return 4;
            case Page::FrameRate:return 3;
            case Page::More:return 5;
            default:return 6;
        }
    }
    const char* title() const {
        switch(page_) {
            case Page::Quality:return "Quality";
            case Page::FrameRate:return "Frame rate";
            case Page::More:return "More settings";
            default:return "Stream settings";
        }
    }
    int command(int row) const {
        if(row<0 || row>=count()) return -99;
        constexpr std::array home{-1,-2,5,8,-3,10};
        constexpr std::array quality{0,1,2,-4};
        constexpr std::array fps{3,4,-4};
        constexpr std::array more{6,7,9,11,-4};
        switch(page_) {
            case Page::Quality:return quality[row];
            case Page::FrameRate:return fps[row];
            case Page::More:return more[row];
            default:return home[row];
        }
    }
    const char* label(int row) const {
        constexpr const char* commands[]{"High quality","Traffic saving","Extreme saving",
            "Frame rate: 30","Frame rate: 60","Performance HUD","中文 / English","Stream diagnostics",
            "Controller settings","Continue to game","Stop + return","Decoder (next session)"};
        const int id=command(row);
        return id>=0 ? commands[id] : id==-1 ? "Quality" : id==-2 ? "Frame rate" :
            id==-3 ? "More settings" : id==-4 ? "Back" : "";
    }
    Selection select(int row,int quality,int fps) {
        const int id=command(row);
        if(id>=0) return {id,row,false};
        if(id==-1) {page_=Page::Quality;return {-1,std::clamp(quality,0,2),true};}
        if(id==-2) {page_=Page::FrameRate;return {-1,fps==30?0:1,true};}
        if(id==-3) {page_=Page::More;return {-1,0,true};}
        if(id==-4) return {-1,*back(),true};
        return {};
    }
    std::optional<int> back() {
        if(page_==Page::Home) return std::nullopt;
        const int focus=page_==Page::Quality ? 0 : page_==Page::FrameRate ? 1 : 4;
        reset();return focus;
    }
private:
    Page page_=Page::Home;
};

// Separate from the 12-row controller editor: hit testing and drawing both
// use this exact geometry, with at least 44px touch targets at 720p.
struct CompactStreamMenuLayout {
    Rect panel,close;
    float row_top=0,row_height=0;
    int count=0;
    CompactStreamMenuLayout(Rect bounds,int rows):count(std::clamp(rows,1,6)) {
        const float width=std::min(440.0f,bounds.w-32);
        const float height=std::min(110.0f+count*46.0f+42.0f,bounds.h-32);
        panel={bounds.x+bounds.w-width-16,bounds.y+16,width,height};
        close={panel.x+panel.w-54,panel.y+6,44,44};
        row_top=panel.y+110;row_height=(panel.h-152)/count;
    }
    Rect row(int index) const {return {panel.x+14,row_top+index*row_height,panel.w-28,row_height};}
    int hit_row(Point point) const {
        for(int i=0;i<count;++i) if(row(i).contains(point)) return i;
        return -1;
    }
};
} // namespace cloudgenshin::overlay
