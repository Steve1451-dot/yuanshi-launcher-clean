#include "mihoyo/initial_dispatch.hpp"
#include "mihoyo/production_profile.hpp"

#include "http_client.hpp"
#include "mihoyo/dispatch_http_error.hpp"

namespace cloudgenshin::dispatch {

DispatchResponse SubmitInitialDispatch(const GameProfile& profile,
                                       const InitialDispatchRequest& request,
                                       const std::atomic_bool* cancelled) {
    if (request.url != std::string(profile.cloud_base)+"/dispatcher/api/paasDispatch" ||
        request.user_agent.empty() ||
        request.body.empty() || request.body.size() > 16384) return {};
    try {
        const auto response = opennow::HttpClient{}.Post(
            request.url, request.user_agent, request.headers, request.body, {},
            {.cancelled = cancelled, .max_body_bytes = 1024 * 1024, .follow_redirects = false});
        return ParseInitialResponse(response.status_code, response.body);
    } catch (const opennow::HttpRequestError& error) {
        return DispatchTransportFailure(error);
    } catch (...) {
        DispatchResponse result;
        result.status = DispatchStatus::TransportError;
        return result;
    }
}

} // namespace cloudgenshin::dispatch
