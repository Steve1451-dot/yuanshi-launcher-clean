#include "mihoyo/queue_ticket.hpp"
#include "mihoyo/production_profile.hpp"

#include "http_client.hpp"
#include "mihoyo/dispatch_http_error.hpp"

namespace cloudgenshin::dispatch {
namespace {

bool ValidRequest(const GameProfile& profile, const QueueRequest& request) {
    const char* path = nullptr;
    switch (request.operation) {
        case QueueOperation::Poll: path = "/dispatcher/api/getDispatchTicketInfo"; break;
        case QueueOperation::Acknowledge: path = "/dispatcher/api/ackDispatchTicket"; break;
        case QueueOperation::Leave: path = "/dispatcher/api/leaveDispatchQueue"; break;
        default: return false;
    }
    return request.url == std::string(profile.cloud_base) + path && !request.body.empty() &&
           request.body.size() <= 8192 && !request.user_agent.empty();
}

opennow::HttpResponse Post(const QueueRequest& request, const std::atomic_bool* cancelled,
                           long timeout_ms = 30000) {
    return opennow::HttpClient{}.Post(request.url, request.user_agent,
        request.headers, request.body, {},
        {.cancelled = cancelled, .max_body_bytes = 1024 * 1024, .follow_redirects = false, .timeout_ms=timeout_ms});
}

} // namespace

DispatchResponse PollQueue(const GameProfile& profile, const QueueRequest& request,
                           const std::atomic_bool* cancelled) {
    if (request.operation != QueueOperation::Poll || !ValidRequest(profile, request))
        return {};
    try {
        const auto response = Post(request, cancelled);
        return ParseTicketResponse(response.status_code, response.body);
    } catch (const opennow::HttpRequestError& error) {
        return DispatchTransportFailure(error);
    } catch (...) {
        DispatchResponse result;
        result.status = DispatchStatus::TransportError;
        return result;
    }
}

QueueActionResult SubmitQueueAction(const GameProfile& profile, const QueueRequest& request,
                                    const std::atomic_bool* cancelled,
                                    long timeout_ms) {
    if (request.operation == QueueOperation::Poll || !ValidRequest(profile, request))
        return {};
    try {
        const auto response = Post(request, cancelled, timeout_ms);
        return ParseQueueActionResponse(response.status_code, response.body);
    } catch (...) {
        return {.status = QueueActionStatus::TransportError};
    }
}

} // namespace cloudgenshin::dispatch
