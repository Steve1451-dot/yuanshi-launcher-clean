#include "mihoyo/dispatch_prepare.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"

namespace cloudgenshin::dispatch {
PrepareResult SubmitPrepare(const GameProfile& profile, const PrepareRequest& request,
                            const std::atomic_bool* cancelled) {
    const char* path = nullptr;
    switch(request.operation) {
        case PrepareOperation::Status: path="statusCheck"; break;
        case PrepareOperation::PingServers: path="listPingServer"; break;
        case PrepareOperation::Nodes: path="getNodesInfo"; break;
        case PrepareOperation::Verify: path="preDispatchVerify"; break;
    }
    if (!path ||
        request.url != std::string(profile.cloud_base)+"/dispatcher/api/"+path ||
        request.body.empty() || request.body.size() > 128*1024) return {};
    try {
        const auto response = opennow::HttpClient{}.Post(request.url,request.user_agent,
            request.headers,request.body,{}, {.cancelled=cancelled,.max_body_bytes=1024*1024,.follow_redirects=false});
        return ParsePrepareResponse(request.operation,response.status_code,response.body);
    } catch (const opennow::HttpRequestError& error) {
        PrepareResult result;result.status=PrepareStatus::TransportError;
        result.transport_error=static_cast<int>(error.curl_code);result.http_status=error.http_status;
        result.transport_diagnostics=error.diagnostics;
        return result;
    } catch (...) { PrepareResult result; result.status=PrepareStatus::TransportError; return result; }
}
} // namespace cloudgenshin::dispatch
