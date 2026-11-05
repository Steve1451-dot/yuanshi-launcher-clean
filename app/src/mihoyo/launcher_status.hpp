#pragma once

#include <string_view>
#include <optional>
#include <utility>

namespace cloudgenshin::launcher {

// Leaving an allocated queue is cleanup, not the outcome of the launch.
// Keep its result in the safe log without replacing a failure, expiry or
// explicit cancellation on the status card. Applies to both native entries.
inline bool IsQueueCleanup(std::string_view event) {
    return event=="QUEUE_LEFT" || event=="QUEUE_LEAVE_FAILED";
}
inline bool PublishesStatus(std::string_view event,bool requested) {
    // Numeric size diagnostics must not reset rank samples, queue ETA or the
    // active status card on every poll. QUEUED_POSITION remains the UI event.
    return requested && !IsQueueCleanup(event) &&
        event!="QUEUE_TOTAL_LENGTH" && event!="QUEUE_BRANCH_LENGTH";
}

inline bool DispatchFailed(std::string_view status) {
    return status=="DISPATCH_BAD_NETWORK" || status=="DISPATCH_HTTP_ERROR" ||
        status=="DISPATCH_NETWORK_ERROR" || status=="DISPATCH_INVALID_FIELDS" ||
        status=="DISPATCH_INVALID_RESPONSE";
}

// User-requested default: leave long ordinary queues, but explain it in a
// one-shot UI notice, never as a login/certificate error or a silent cancel.
inline bool LeavesLongQueue(int position) {return position>100;}
struct QueueExit {
    int position;
    bool confirmed=false;
};
class QueueExitNotice {
public:
    void clear() {pending_.reset();}
    void observe(std::string_view event,int code) {
        if(event=="QUEUE_ABANDONED_TOO_LONG" && LeavesLongQueue(code)) pending_=QueueExit{code};
        else if(pending_ && IsQueueCleanup(event)) pending_->confirmed=event=="QUEUE_LEFT";
    }
    std::optional<QueueExit> take(bool working) {
        // The worker must finish the leave request before opening a dialog;
        // pushing an activity while it runs would cancel/join it from the UI.
        if(working) return std::nullopt;
        return std::exchange(pending_,std::nullopt);
    }
private:
    std::optional<QueueExit> pending_;
};

} // namespace cloudgenshin::launcher
