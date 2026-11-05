#include "mihoyo/gamer_login.hpp"
#include "mihoyo/production_profile.hpp"

#include "http_client.hpp"

namespace cloudgenshin::auth {

GamerLoginResult CheckGamerLogin(const GameProfile& profile, const GamerLoginRequest& request,
                               const std::atomic_bool* cancelled) {
    if (request.url != std::string(profile.cloud_base) + "/gamer/api/login")
        return {GamerLoginStatus::InvalidResponse};
    try {
        opennow::HttpClient client;
        const auto response = client.Post(request.url, request.user_agent,
                                          request.headers, {}, {},
                                          {.cancelled = cancelled,
                                           .max_body_bytes = 1024 * 1024,
                                           .follow_redirects = false});
        return ParseGamerLoginResponse(response.status_code, response.body);
    } catch (const opennow::HttpRequestError& error) {
        return {GamerLoginStatus::TransportError,error.http_status,0,static_cast<int>(error.curl_code)};
    } catch (...) {
        return {GamerLoginStatus::TransportError};
    }
}

} // namespace cloudgenshin::auth
