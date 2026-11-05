#pragma once

#include "mihoyo/auth_header.hpp"

#include <optional>
#include <atomic>
#include <string>
#include <vector>

namespace cloudgenshin {
struct GameProfile;
} // namespace cloudgenshin

namespace cloudgenshin::auth {

struct WebDeviceIdentity {
    std::string user_agent;
    std::string client_type;
    std::string app_version;
    std::string system_version;
    std::string device_id;
    std::string device_name;
    std::string device_model;
    std::string op_biz;
    std::string language;
    std::string cps;
};

// The response to this endpoint is used only as an authorization check by the
// official web client. Neither credentials nor response bodies are logged.
struct GamerLoginRequest {
    std::string url;
    std::string user_agent;
    std::vector<std::string> headers;
};

enum class GamerLoginStatus {
    Success,
    Rejected,
    HttpError,
    InvalidResponse,
    TransportError,
};

struct GamerLoginResult {
    GamerLoginStatus status;
    long http_status = 0;
    int retcode = 0;
    int transport_error = 0; // Numeric CURLcode only; never exception/request text.
};

std::optional<GamerLoginRequest> BuildGamerLoginRequest(
    const GameProfile& profile, const ComboLoginFields& fields,
    const WebDeviceIdentity& device);

GamerLoginResult ParseGamerLoginResponse(long http_status, const std::string& body);

// Performs the official fixed-host POST using the project's TLS-verifying
// HttpClient. Call only after a user-authorized combo login supplies fields.
GamerLoginResult CheckGamerLogin(const GameProfile& profile, const GamerLoginRequest& request,
                               const std::atomic_bool* cancelled = nullptr);

} // namespace cloudgenshin::auth
