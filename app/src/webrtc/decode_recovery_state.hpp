#pragma once

#include <cstdint>
#include <optional>
#include <utility>

namespace opennow::webrtc
{

class DecodeRecoveryState
{
public:
    using Generation = uint64_t;

    enum class Completion
    {
        Accepted,
        Stale,
        Failed,
    };

    void require_resync()
    {
        ++generation_;
        waiting_for_idr_ = true;
        reset_pending_ = true;
        keyframe_pending_ = true;
    }

    // An RTP access unit was destroyed by packet loss after the repair window
    // expired. Stop admitting dependent pictures immediately, but keep the
    // generation unchanged: already queued complete units are still valid and
    // must be allowed to decode. The next IDR ends the gate. No decoder reset
    // is scheduled because corrupt bytes were never submitted.
    bool mark_damaged()
    {
        const bool transition = !waiting_for_idr_;
        waiting_for_idr_ = true;
        keyframe_pending_ = true;
        return transition;
    }

    std::optional<Generation> admit(bool idr)
    {
        if (waiting_for_idr_) {
            if (!idr)
                return std::nullopt;
            waiting_for_idr_ = false;
            keyframe_pending_ = false;
        }
        return generation_;
    }

    bool is_current(Generation generation) const
    {
        return generation == generation_;
    }

    Generation generation() const {return generation_;}

    Completion complete(Generation generation, bool success)
    {
        if (!is_current(generation))
            return Completion::Stale;
        if (!success) {
            require_resync();
            return Completion::Failed;
        }
        return Completion::Accepted;
    }

    bool waiting_for_idr() const
    {
        return waiting_for_idr_;
    }

    bool take_decoder_reset()
    {
        return std::exchange(reset_pending_, false);
    }

    bool take_keyframe_request()
    {
        return std::exchange(keyframe_pending_, false);
    }

    void defer_keyframe_request()
    {
        if (waiting_for_idr())
            keyframe_pending_ = true;
    }

private:
    Generation generation_ = 0;
    bool waiting_for_idr_ = false;
    bool reset_pending_ = false;
    bool keyframe_pending_ = false;
};

}
