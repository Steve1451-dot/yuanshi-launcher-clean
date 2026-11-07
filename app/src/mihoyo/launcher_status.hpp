#pragma once

#include <string_view>

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

// Keep the existing long-queue exit rule. Its result stays on the status card;
// no modal notice interrupts subsequent launcher actions.
inline bool LeavesLongQueue(int position) {return position>100;}

} // namespace cloudgenshin::launcher
