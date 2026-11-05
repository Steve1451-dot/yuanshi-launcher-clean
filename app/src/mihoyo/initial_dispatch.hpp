#pragma once

#include "mihoyo/dispatch_response.hpp"
#include "mihoyo/gamer_login.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace cloudgenshin::dispatch {

enum class QueueType { Normal, Coin };

// Supplied after status/ping/node selection and user-driven authorization.
// user_data_json is the compact JSON object used as the web payload's
// user_data string, not a JSON object inserted directly into the request.
struct InitialDispatchParams {
    std::uint32_t bit_rate = 0;
    int fps = 0;
    std::string node_id;
    std::string resolution;
    std::string environment;
    std::string user_data_json;
    std::string regions_json = "[]";
    unsigned net_state = 4;
    QueueType queue_type = QueueType::Normal;
    bool queue_switch = false;
    std::optional<std::string> hint;
};

// Contains authorization and potentially identifying device information.
// Keep only in memory; never include it in diagnostic logs or persistence.
struct InitialDispatchRequest {
    std::string url;
    std::string user_agent;
    std::vector<std::string> headers;
    std::string body;
};

std::optional<InitialDispatchRequest> BuildInitialDispatchRequest(
    const GameProfile& profile, const auth::ComboLoginFields& fields,
    const auth::WebDeviceIdentity& device, const InitialDispatchParams& params);

DispatchResponse SubmitInitialDispatch(const GameProfile& profile,
                                       const InitialDispatchRequest& request,
                                       const std::atomic_bool* cancelled = nullptr);

} // namespace cloudgenshin::dispatch
