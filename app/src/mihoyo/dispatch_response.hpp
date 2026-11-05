#pragma once

#include "mihoyo/sdk_start_game_params.hpp"
#include "http_transfer_diagnostics.hpp"

#include <optional>
#include <string>

namespace cloudgenshin::dispatch {

enum class DispatchStatus {
    Finished,
    Queued,
    Unavailable,
    SwitchUnavailable,
    Maintenance,
    BadNetwork,
    Failed,
    DeviceConflict,
    KickedOut,
    TimedOut,
    ApiRejected,
    HttpError,
    TransportError,
    InvalidResponse,
};

// Session identifiers and queue tickets remain in memory only. Callers must
// never include this structure or raw response bodies in diagnostic logs.
struct DispatchResponse {
    DispatchStatus status = DispatchStatus::InvalidResponse;
    int retcode = 0;
    long http_status = 0;
    std::string ticket;
    std::string sid;
    std::string cloud_provider;
    std::optional<SdkStartGameParams> sdk_start_game_params;
    int query_interval_seconds = 0;
    // Official DispatchQueueInfo keeps personal rank separate from queue size.
    // Never substitute either length for rank when displaying or leaving a queue.
    std::optional<int> queue_rank;
    std::optional<int> queue_length;
    std::optional<int> branch_queue_length;
    bool requires_ticket_ack = false;
    int transport_error = 0; // CURLcode only, never exception text or a URL.
    bool retryable_transport = false;
    opennow::HttpTransferDiagnostics transport_diagnostics;
};

// The initial paasDispatch and getDispatchTicketInfo responses have different
// status fields. Do not use one parser for the other endpoint.
DispatchResponse ParseInitialResponse(long http_status, const std::string& body);
DispatchResponse ParseTicketResponse(long http_status, const std::string& body);

} // namespace cloudgenshin::dispatch
