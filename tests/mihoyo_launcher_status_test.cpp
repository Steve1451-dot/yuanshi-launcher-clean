#include "mihoyo/launcher_status.hpp"
#include <cassert>
#include <string_view>

int main() {
    using namespace cloudgenshin::launcher;
    for(const auto* metric:{"QUEUE_TOTAL_LENGTH","QUEUE_BRANCH_LENGTH"}) {
        assert(!PublishesStatus(metric,true) && !PublishesStatus(metric,false));
    }
    assert(!LeavesLongQueue(-1) && !LeavesLongQueue(0) && !LeavesLongQueue(100));
    assert(LeavesLongQueue(101) && LeavesLongQueue(243) && LeavesLongQueue(327));
    QueueExitNotice notice;
    notice.observe("QUEUED_POSITION",243);assert(!notice.take(false));
    notice.observe("QUEUE_ABANDONED_TOO_LONG",243);
    assert(!notice.take(true)); // No modal push or worker join during cleanup.
    notice.observe("QUEUE_LEFT",0);
    const auto exited=notice.take(false);
    assert(exited && exited->position==243 && exited->confirmed);
    assert(!notice.take(false)); // Once per attempt, not every draw frame.
    notice.observe("QUEUE_ABANDONED_TOO_LONG",327);
    notice.observe("QUEUE_LEAVE_FAILED",28);
    const auto unconfirmed=notice.take(false);
    assert(unconfirmed && unconfirmed->position==327 && !unconfirmed->confirmed);
    notice.observe("QUEUE_ABANDONED_TOO_LONG",233);notice.clear();
    assert(!notice.take(false)); // A new attempt never inherits the old modal.
    notice.observe("LAUNCH_CANCELLED",0);notice.observe("QUEUE_LEFT",0);
    assert(!notice.take(false)); // Manual cancel is not a long-queue exit.
    for(const auto* cleanup:{"QUEUE_LEFT","QUEUE_LEAVE_FAILED"}) {
        assert(!PublishesStatus(cleanup,true));
        assert(!PublishesStatus(cleanup,false));
        // Same policy used by State::event: cleanup after an unsuccessful
        // launch must not turn its actual outcome into "Cancelled".
        for(const auto* outcome:{"DISPATCH_NETWORK_ERROR","QUEUE_ACK_FAILED",
                "QUEUE_EXPIRED","DISPATCH_REJECTED","LAUNCH_CANCELLED","QUEUE_ABANDONED_TOO_LONG"}) {
            std::string_view visible=outcome;
            if(PublishesStatus(cleanup,true)) visible=cleanup;
            assert(visible==outcome);
        }
    }
    for(const auto* normal:{"QUEUED_POSITION","DISPATCH_READY_FOR_GAME_AUTH",
            "CANCEL_REQUESTED","LAUNCH_CANCELLED","GET_NODES_FAILED",
            "ENDFIELD_GAME_AUTH_VERIFICATION_REQUIRED"}) {
        assert(PublishesStatus(normal,true));
        assert(!PublishesStatus(normal,false));
    }
    for(const auto* failed:{"DISPATCH_BAD_NETWORK","DISPATCH_HTTP_ERROR",
            "DISPATCH_NETWORK_ERROR","DISPATCH_INVALID_FIELDS","DISPATCH_INVALID_RESPONSE"})
        assert(DispatchFailed(failed));
    for(const auto* normal:{"DISPATCH_NORMAL_QUEUE","DISPATCH_DEFAULT_GAMEPAD_REQUESTED",
            "DISPATCH_READY_FOR_GAME_AUTH","QUEUED_POSITION","QUEUE_LEFT","LOGIN_OK"})
        assert(!DispatchFailed(normal));
}
