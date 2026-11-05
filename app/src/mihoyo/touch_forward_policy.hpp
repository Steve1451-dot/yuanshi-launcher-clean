#pragma once
// Game touch forwarding policy: turns the platform's per-frame pressed
// contacts into the official RtcTouchEvent stream (Down/Move/Up) used by the
// cloud SDK's TouchController. The wire encoder lives in input_packet.*; this
// tracker owns the state transitions so they stay host-testable and free of
// platform dependencies.
#include "mihoyo/input_packet.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace cloudgenshin::touch {

// A finger the platform currently reports as pressed, already normalised
// against the video rectangle. Contacts the overlay owns (floating ball, open
// panel) never reach this policy.
struct Contact {
    std::uint32_t finger_id = 0;
    double x = 0.0;
    double y = 0.0;
};

// Bound the tracked set: the Switch panel reports at most 10 fingers.
inline constexpr std::size_t kMaxTracked = 10;

// Tracks sampled contacts. The stream view owns the pending event delivery;
// update() commits the sample here without assuming a successful network send.
class TouchTracker {
public:
    // Events to send for this frame: releases first, then new downs, then
    // moves. At most kTouchEventListLimit events; surplus moves drop because
    // the next frame follows within 16 ms.
    std::vector<input::TouchEvent> update(const std::vector<Contact>& contacts) {
        std::vector<input::TouchEvent> out;
        // Fingers that vanished since the last frame are released first so the
        // game never sees a contradicting hold.
        for (auto it = tracked_.begin(); it != tracked_.end();) {
            if (Contains(contacts, it->first)) {++it; continue;}
            out.push_back(Event(it->first, input::kTouchActionUp,
                                it->second.x, it->second.y, it->first == primary_));
            if (it->first == primary_) primary_.reset();
            it = tracked_.erase(it);
        }
        // New fingers become Down. Only the gesture's first finger is primary,
        // matching PointerEvent.isPrimary; after the primary lifts, the next
        // new finger becomes primary while held fingers stay secondary.
        for (const auto& contact : contacts) {
            if (out.size() >= input::kTouchEventListLimit ||
                tracked_.size() >= kMaxTracked) break;
            if (tracked_.find(contact.finger_id) != tracked_.end()) continue;
            if (!primary_) primary_ = contact.finger_id;
            tracked_.emplace(contact.finger_id, contact);
            out.push_back(Event(contact.finger_id, input::kTouchActionDown,
                                contact.x, contact.y, contact.finger_id == primary_));
        }
        // A moved finger reports once per frame at most.
        for (const auto& contact : contacts) {
            if (out.size() >= input::kTouchEventListLimit) break;
            const auto it = tracked_.find(contact.finger_id);
            if (it == tracked_.end()) continue;
            if (it->second.x == contact.x && it->second.y == contact.y) continue;
            it->second = contact;
            out.push_back(Event(contact.finger_id, input::kTouchActionMove,
                                contact.x, contact.y, contact.finger_id == primary_));
        }
        return out;
    }

    // Releases every tracked finger; the primary finger's Up carries
    // isPrimary. Used when a UI panel owns the screen or the stream exits, so
    // the game is not left holding a contact whose Up will never arrive.
    std::vector<input::TouchEvent> release_all() {
        std::vector<input::TouchEvent> out;
        for (const auto& entry : tracked_)
            if (entry.first != primary_)
                out.push_back(Event(entry.first, input::kTouchActionUp,
                                    entry.second.x, entry.second.y, false));
        if (primary_) {
            const auto it = tracked_.find(*primary_);
            if (it != tracked_.end())
                out.push_back(Event(*primary_, input::kTouchActionUp,
                                    it->second.x, it->second.y, true));
        }
        tracked_.clear();
        primary_.reset();
        return out;
    }

    std::size_t tracked() const {return tracked_.size();}

private:
    static bool Contains(const std::vector<Contact>& contacts, std::uint32_t id) {
        for (const auto& contact : contacts)
            if (contact.finger_id == id) return true;
        return false;
    }
    static input::TouchEvent Event(std::uint32_t finger_id, std::uint32_t action,
                                   double x, double y, bool primary) {
        input::TouchEvent event;
        event.finger_id = finger_id;
        event.action = action;
        event.x = x;
        event.y = y;
        event.primary = primary;
        return event;
    }

    std::map<std::uint32_t, Contact> tracked_;
    // Zero is a valid platform finger id, not an empty primary slot.
    std::optional<std::uint32_t> primary_;
};

} // namespace cloudgenshin::touch
