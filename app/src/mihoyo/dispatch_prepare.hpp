#pragma once
#include "mihoyo/gamer_login.hpp"
#include "http_transfer_diagnostics.hpp"
#include <atomic>
#include <optional>
#include <vector>

namespace cloudgenshin::dispatch {
enum class PrepareOperation { Status, PingServers, Nodes, Verify };
enum class PrepareStatus { Success, Maintenance, Unavailable, Rejected, InvalidResponse, HttpError, TransportError };
struct Node {
    std::string id;
    std::string regions_json;
    bool recommended = false;
};
struct PrepareResult {
    PrepareStatus status = PrepareStatus::InvalidResponse;
    int retcode = 0;
    long http_status = 0;
    std::vector<Node> nodes;
    std::string ping_sdk_param;
    int transport_error = 0; // Numeric CURLcode only, never exception text.
    opennow::HttpTransferDiagnostics transport_diagnostics;
};
struct PrepareRequest {
    PrepareOperation operation;
    std::string url, user_agent, body;
    std::vector<std::string> headers;
};
std::optional<PrepareRequest> BuildPrepareRequest(const GameProfile& profile,
    PrepareOperation operation,
    const auth::ComboLoginFields& fields, const auth::WebDeviceIdentity& device,
    const Node& node = {}, const std::string& measured_ping_node = {});
PrepareResult ParsePrepareResponse(PrepareOperation operation, long http_status,
                                   const std::string& body);
PrepareResult SubmitPrepare(const GameProfile& profile, const PrepareRequest& request,
                            const std::atomic_bool* cancelled);
} // namespace cloudgenshin::dispatch
