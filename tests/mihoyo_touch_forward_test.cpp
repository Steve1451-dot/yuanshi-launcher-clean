#include "mihoyo/input_packet.hpp"
#include "mihoyo/touch_forward_policy.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

int main() {
    using cloudgenshin::input::kTouchActionDown;
    using cloudgenshin::input::kTouchActionMove;
    using cloudgenshin::input::kTouchActionUp;
    using cloudgenshin::touch::Contact;
    cloudgenshin::touch::TouchTracker tracker;

    // First finger: Down, primary, positions pass through unchanged.
    auto events=tracker.update({{7,0.25,0.5}});
    assert(events.size()==1);
    assert(events[0].finger_id==7 && events[0].action==kTouchActionDown);
    assert(events[0].x==0.25 && events[0].y==0.5 && events[0].primary);

    // An unchanged frame sends nothing.
    assert(tracker.update({{7,0.25,0.5}}).empty());

    // A moved finger: Move with the fresh position, still primary.
    events=tracker.update({{7,0.3,0.4}});
    assert(events.size()==1 && events[0].action==kTouchActionMove);
    assert(events[0].x==0.3 && events[0].y==0.4 && events[0].primary);

    // A second finger: Down, not primary; the first finger is untouched.
    events=tracker.update({{7,0.3,0.4},{9,0.6,0.6}});
    assert(events.size()==1 && events[0].finger_id==9);
    assert(events[0].action==kTouchActionDown && !events[0].primary);

    // Releasing the primary mid-gesture: Up for 7 only; 9 stays secondary and
    // the next new finger becomes primary (PointerEvent.isPrimary semantics).
    events=tracker.update({{9,0.6,0.6}});
    assert(events.size()==1 && events[0].finger_id==7);
    assert(events[0].action==kTouchActionUp && events[0].primary);
    events=tracker.update({{9,0.6,0.6},{3,0.1,0.1}});
    assert(events.size()==1 && events[0].finger_id==3 && events[0].primary);

    // A UI panel taking the screen releases every tracked finger; secondary
    // fingers first, the primary finger's Up last.
    events=tracker.release_all();
    assert(events.size()==2 && tracker.tracked()==0);
    assert(events[0].finger_id==9 && !events[0].primary);
    assert(events[1].finger_id==3 && events[1].action==kTouchActionUp &&
           events[1].primary);

    // The 10-event batch cap: a release plus a new finger plus nine moves
    // exceeds one frame, so the last move slips to the next frame and is not
    // lost (positions are re-sampled every 16 ms).
    cloudgenshin::touch::TouchTracker full;
    std::vector<Contact> ten;
    for(std::uint32_t i=0;i<10;++i) ten.push_back({i+1,0.1*i,0.2});
    assert(full.update(ten).size()==10);
    std::vector<Contact> crowded;
    for(std::uint32_t i=1;i<10;++i) crowded.push_back({i+1,0.1*i,0.9});
    crowded.push_back({11,0.5,0.5});
    events=full.update(crowded);
    assert(events.size()==10);
    assert(events[0].finger_id==1 && events[0].action==kTouchActionUp &&
           events[0].primary);
    assert(events[1].finger_id==11 && events[1].action==kTouchActionDown &&
           events[1].primary);
    assert(events[2].finger_id==2 && events[2].action==kTouchActionMove);
    assert(events.back().finger_id==9);
    events=full.update(crowded);
    assert(events.size()==1 && events[0].finger_id==10 &&
           events[0].action==kTouchActionMove);

    // Tracker output is exactly what the official channel encoder accepts.
    const auto framed=cloudgenshin::input::EncodeInputPacket(
        cloudgenshin::input::EncodeTouchEventListChannel(events));
    assert(cloudgenshin::input::IsValidControlPacket(framed));
}
