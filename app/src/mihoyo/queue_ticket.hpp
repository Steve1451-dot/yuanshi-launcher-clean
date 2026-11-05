#pragma once

#include "mihoyo/dispatch_response.hpp"
#include "mihoyo/gamer_login.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cloudgenshin::dispatch {

enum class QueueOperation { Poll, Acknowledge, Leave };

// Contains a signed ticket and authorization headers. Keep in memory only;
// never log, persist, or include it in an error message.
struct QueueRequest {
    QueueOperation operation;
    std::string url;
    std::string user_agent;
    std::vector<std::string> headers;
    std::string body;
};

std::optional<QueueRequest> BuildQueueRequest(
    const GameProfile& profile, QueueOperation operation,
    const auth::ComboLoginFields& fields, const auth::WebDeviceIdentity& device,
    const std::string& ticket);

enum class QueueActionStatus {
    Success,
    ApiRejected,
    HttpError,
    InvalidResponse,
    TransportError,
};

struct QueueActionResult {
    QueueActionStatus status = QueueActionStatus::InvalidResponse;
    long http_status = 0;
    int retcode = 0;
};

QueueActionResult ParseQueueActionResponse(long http_status, const std::string& body);

// These fixed-host requests are available once user-driven official combo
// login provides live authorization. No request is made while building.
DispatchResponse PollQueue(const GameProfile& profile, const QueueRequest& request,
                           const std::atomic_bool* cancelled = nullptr);
QueueActionResult SubmitQueueAction(const GameProfile& profile, const QueueRequest& request,
                                    const std::atomic_bool* cancelled = nullptr,
                                    long timeout_ms = 30000);

} // namespace cloudgenshin::dispatch
