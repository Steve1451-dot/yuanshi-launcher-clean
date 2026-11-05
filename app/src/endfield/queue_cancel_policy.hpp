#pragma once

namespace cloudgenshin::endfield {
// APK CloudServerErrorCode.SESSION_NOT_QUEUING=23001. /session/cancel is
// CANCEL_QUEUE, not a game-stop API. Never retry with a guessed alternate id
// or turn this response into proof that a running pod has been released.
inline const char* QueueCancelEvent(long long code) {
    if(code==0) return "ENDFIELD_QUEUE_CANCELLED";
    if(code==23001) return "ENDFIELD_QUEUE_NOT_ACTIVE";
    return "ENDFIELD_QUEUE_CANCEL_FAILED";
}
} // namespace cloudgenshin::endfield
