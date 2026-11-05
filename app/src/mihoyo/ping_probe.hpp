#pragma once
#include "http_transfer_diagnostics.hpp"
#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cloudgenshin::dispatch {
// Server-issued SDK ping metadata remains in RAM, never in safe logs.
struct PingServer {
    std::uint32_t id=0,isp_type=0;
    std::int64_t province_id=0;
    std::string addr,display_name,region_id,node_id,wss_url;
    bool ipv6=false;
};
struct PingSample { PingServer server; unsigned rtt_ms=0; };
struct PingProbeResult {
    std::string node_json;
    unsigned tested=0,succeeded=0;
    // Aggregated numeric diagnostics only: no endpoint, node id or response.
    unsigned invalid_endpoints=0,connect_failures=0,echo_failures=0,connect_timeouts=0;
    int first_curl_code=0;
    long first_http_status=0;
    opennow::HttpTransferDiagnostics first_transport{};
    int first_transport_stage=0,first_elapsed_ms=0,first_port=0,first_os_error=0;
};
std::optional<std::vector<PingServer>> ParsePingServers(std::span<const std::uint8_t> protobuf);
std::optional<std::vector<PingServer>> DecodePingServers(const std::string& base64);
std::optional<std::string> BuildMeasuredPingNode(const std::vector<PingSample>& samples);
PingProbeResult ProbePingServers(const std::vector<PingServer>& servers,const std::atomic_bool& cancelled);
std::optional<unsigned> ProbePingServer(const PingServer& server,const std::atomic_bool& cancelled,
                                       long budget_ms=2500);
} // namespace cloudgenshin::dispatch
