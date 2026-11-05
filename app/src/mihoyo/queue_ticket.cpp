#include "mihoyo/queue_ticket.hpp"
#include "mihoyo/production_profile.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <string_view>

namespace cloudgenshin::dispatch {
namespace {

bool SafeTicket(std::string_view ticket) {
    if (ticket.empty() || ticket.size() > 4096) return false;
    for (const unsigned char c : ticket)
        if (c < 0x20 || c == 0x7f) return false;
    return true;
}

std::optional<int> ResponseRetcode(const nlohmann::json& json) {
    if (!json.is_object() || !json.contains("retcode") ||
        !json["retcode"].is_number_integer()) return std::nullopt;
    const auto& code = json["retcode"];
    if (code.is_number_unsigned()) {
        const auto value = code.get<std::uint64_t>();
        if (value > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return std::nullopt;
        return static_cast<int>(value);
    }
    const auto value = code.get<std::int64_t>();
    if (value < std::numeric_limits<int>::min() ||
        value > std::numeric_limits<int>::max()) return std::nullopt;
    return static_cast<int>(value);
}

} // namespace

std::optional<QueueRequest> BuildQueueRequest(
    const GameProfile& profile, QueueOperation operation,
    const auth::ComboLoginFields& fields, const auth::WebDeviceIdentity& device,
    const std::string& ticket) {
    if (!SafeTicket(ticket)) return std::nullopt;
    const auto login = auth::BuildGamerLoginRequest(profile, fields, device);
    if (!login) return std::nullopt;
    const std::string signed_fields = "biz_key=" + std::string(profile.game_biz) +
                                      "&ticket=" + ticket;
    const auto signature = auth::HmacSha256Hex(std::string(profile.dispatch_app_key), signed_fields);
    if (!signature) return std::nullopt;

    std::string_view path;
    switch (operation) {
        case QueueOperation::Poll: path = "/dispatcher/api/getDispatchTicketInfo"; break;
        case QueueOperation::Acknowledge: path = "/dispatcher/api/ackDispatchTicket"; break;
        case QueueOperation::Leave: path = "/dispatcher/api/leaveDispatchQueue"; break;
        default: return std::nullopt;
    }
    QueueRequest request{operation, std::string(profile.cloud_base) + std::string(path),
                         login->user_agent, login->headers, {}};
    request.headers.push_back("Content-Type: application/json");
    request.body = nlohmann::json{{"biz_key", std::string(profile.game_biz)},
                                  {"ticket", ticket}, {"sign", *signature}}.dump();
    return request;
}

QueueActionResult ParseQueueActionResponse(long http_status, const std::string& body) {
    QueueActionResult result;
    result.http_status = http_status;
    if (http_status < 200 || http_status >= 300) {
        result.status = QueueActionStatus::HttpError;
        return result;
    }
    if (body.empty() || body.size() > 1024 * 1024) return result;
    const auto json = nlohmann::json::parse(body, nullptr, false);
    const auto code = ResponseRetcode(json);
    if (!code) return result;
    result.retcode = *code;
    result.status = *code == 0 ? QueueActionStatus::Success :
                                 QueueActionStatus::ApiRejected;
    return result;
}

} // namespace cloudgenshin::dispatch
