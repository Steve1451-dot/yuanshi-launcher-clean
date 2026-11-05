#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <optional>

namespace cloudgenshin::overlay {
struct Point {float x=0,y=0;};
struct Rect {
    float x=0,y=0,w=0,h=0;
    bool contains(Point p) const {return p.x>=x && p.y>=y && p.x<x+w && p.y<y+h;}
};
inline constexpr int kMenuRows=12;
struct Layout {
    Rect viewport,panel,close;
    float row_top=0,row_height=0;
    explicit Layout(Rect bounds):viewport(bounds) {
        panel={bounds.x+bounds.w-std::min(540.0f,bounds.w-32.0f)-16.0f,
            bounds.y+16.0f,std::min(540.0f,bounds.w-32.0f),std::min(680.0f,bounds.h-32.0f)};
        close={panel.x+panel.w-58,panel.y+8,48,48};
        row_top=panel.y+190;
        row_height=(panel.h-242)/kMenuRows;
    }
    Rect row(int index) const {return {panel.x+14,row_top+index*row_height,panel.w-28,row_height};}
    int hit_row(Point p) const {
        for(int i=0;i<kMenuRows;++i) if(row(i).contains(p)) return i;
        return -1;
    }
};

// One finger owns a gesture until all fingers are lifted. Starting elsewhere
// cannot turn into a ball tap, and dragging cannot accidentally activate it.
class FloatingBall {
public:
    Point centre(Rect bounds) {
        if(!initialized_) {centre_={bounds.x+bounds.w-42,bounds.y+88};initialized_=true;}
        clamp(bounds);return centre_;
    }
    bool hit(Point p,Rect bounds) {const auto c=centre(bounds);return distance(p,c)<=32*32;}
    bool update(bool down,int finger,Point p,Rect bounds) {
        centre(bounds);
        if(!down) {
            const bool tap=captured_ && !dragged_;
            if(captured_ && dragged_) {
                centre_.x=centre_.x<bounds.x+bounds.w/2 ? bounds.x+42 : bounds.x+bounds.w-42;
                clamp(bounds);
            }
            held_=captured_=dragged_=false;return tap;
        }
        if(!held_) {
            held_=true;finger_=finger;captured_=hit(p,bounds);start_=p;origin_=centre_;
        }
        if(captured_ && finger!=finger_) {captured_=false;return false;}
        if(captured_) {
            if(distance(p,start_)>12*12) dragged_=true;
            if(dragged_) {centre_={origin_.x+p.x-start_.x,origin_.y+p.y-start_.y};clamp(bounds);}
        }
        return false;
    }
    bool captured() const {return captured_;}
    void cancel() {captured_=false;dragged_=false;} // Still wait for full release.
private:
    static float distance(Point a,Point b) {return (a.x-b.x)*(a.x-b.x)+(a.y-b.y)*(a.y-b.y);}
    void clamp(Rect b) {
        centre_.x=std::clamp(centre_.x,b.x+34,std::max(b.x+34,b.x+b.w-34));
        centre_.y=std::clamp(centre_.y,b.y+34,std::max(b.y+34,b.y+b.h-34));
    }
    Point centre_,start_,origin_;
    int finger_=0;
    bool initialized_=false,held_=false,captured_=false,dragged_=false;
};

class MenuTap {
public:
    // Targets: menu rows, -2 close/outside, -1 inert. Commit on release only.
    std::optional<int> update(bool down,int finger,int target) {
        if(!down) {
            const auto result=held_ && valid_ ? std::optional<int>(target_) : std::nullopt;
            held_=valid_=false;return result;
        }
        if(!held_) {held_=true;finger_=finger;target_=target;valid_=target!=-1;}
        else if(finger!=finger_ || target!=target_) valid_=false;
        return std::nullopt;
    }
    void cancel() {held_=true;valid_=false;} // Ignore a touch already held when opening/closing.
private:
    int finger_=0,target_=-1;
    bool held_=false,valid_=false;
};

// The worker completes only the revision it sent. A late success/failure must
// not overwrite a newer UI selection. Sent means local send, never server ACK.
class SettingRequest {
public:
    enum class State : std::uint64_t {Unset,Queued,Sent,Failed};
    struct Snapshot {
        std::uint64_t bits;
        State state() const {return static_cast<State>(bits&3);}
        int value() const {return static_cast<int>((bits>>2)&255);}
    };
    void request(int value) {
        auto old=bits_.load();
        while(!bits_.compare_exchange_weak(old,(((old>>10)+1)<<10) |
            (static_cast<std::uint64_t>(value&255)<<2) | static_cast<unsigned>(State::Queued))) {}
    }
    Snapshot snapshot() const {return {bits_.load()};}
    std::optional<Snapshot> pending() const {
        const auto s=snapshot();return s.state()==State::Queued ? std::optional<Snapshot>(s) : std::nullopt;
    }
    void complete(Snapshot request,bool success) {
        auto expected=request.bits;
        bits_.compare_exchange_strong(expected,(request.bits&~std::uint64_t{3}) |
            static_cast<unsigned>(success ? State::Sent : State::Failed));
    }
private:
    std::atomic_uint64_t bits_{0};
};

class StreamRates {
public:
    bool sample(std::int64_t now_ms,unsigned decoded,std::uint64_t bytes) {
        if(!started_) {started_=true;last_ms_=now_ms;decoded_=decoded;bytes_=bytes;return false;}
        const auto elapsed=now_ms-last_ms_;
        if(elapsed<1000) return false;
        fps_=decoded>=decoded_ ? (decoded-decoded_)*1000.0/elapsed : 0;
        mbps_=bytes>=bytes_ ? (bytes-bytes_)*0.008/elapsed : 0;
        last_ms_=now_ms;decoded_=decoded;bytes_=bytes;return true;
    }
    double fps() const {return fps_;}
    double mbps() const {return mbps_;} // Completed video payload only, not network RTT/bandwidth.
private:
    bool started_=false;
    std::int64_t last_ms_=0;
    unsigned decoded_=0;
    std::uint64_t bytes_=0;
    double fps_=0,mbps_=0;
};
} // namespace cloudgenshin::overlay
