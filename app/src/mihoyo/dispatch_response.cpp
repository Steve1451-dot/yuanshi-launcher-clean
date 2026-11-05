#include "mihoyo/dispatch_response.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

namespace cloudgenshin::dispatch {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kMaximumResponseBytes = 1024 * 1024;
constexpr std::size_t kMaximumTicketBytes = 4096;
constexpr std::size_t kMaximumSessionBytes = 4096;

std::optional<int> Integer(const Json& value) {
    if (!value.is_number_integer()) return std::nullopt;
    if (value.is_number_unsigned()) {
        const auto number = value.get<std::uint64_t>();
        if (number > static_cast<std::uint64_t>(std::numeric_limits<int>::max()))
            return std::nullopt;
        return static_cast<int>(number);
    }
    const auto number = value.get<std::int64_t>();
    if (number < std::numeric_limits<int>::min() ||
        number > std::numeric_limits<int>::max()) return std::nullopt;
    return static_cast<int>(number);
}

std::optional<std::string> BoundedString(const Json& object, const char* key,
                                         std::size_t maximum) {
    if (!object.is_object() || !object.contains(key) || !object[key].is_string())
        return std::nullopt;
    const auto value = object[key].get<std::string>();
    if (value.empty() || value.size() > maximum) return std::nullopt;
    for (const unsigned char c : value)
        if (c < 0x20 || c == 0x7f) return std::nullopt;
    return value;
}

std::optional<int> OptionalQueueNumber(const Json& info, const char* key) {
    if (!info.contains(key)) return std::nullopt;
    const auto& value = info[key];
    if (const auto number = Integer(value); number && *number >= 0)
        return number;
    if (value.is_string()) {
        const auto text = value.get<std::string>();
        if (text.empty() || text.size() > 10) return std::nullopt;
        int number = 0;
        for (const char c : text) {
            if (c < '0' || c > '9') return std::nullopt;
            if (number > (std::numeric_limits<int>::max() - (c - '0')) / 10)
                return std::nullopt;
            number = number * 10 + (c - '0');
        }
        return number;
    }
    return std::nullopt;
}

bool ReadFinished(const Json& data, DispatchResponse& result, bool ticket_ack) {
    if (!data.contains("finish_result") || !data["finish_result"].is_object())
        return false;
    const auto& finished = data["finish_result"];
    // Android's DispatchFinishResult uses these serialized-name aliases.
    // Reject conflicting copies rather than silently choosing credentials.
    const auto read_alias = [&](const char* primary, const char* alias,
                                std::size_t maximum) -> std::optional<std::string> {
        const bool has_primary = finished.contains(primary);
        const bool has_alias = finished.contains(alias);
        const auto primary_value = BoundedString(finished, primary, maximum);
        const auto alias_value = BoundedString(finished, alias, maximum);
        if ((has_primary && !primary_value) || (has_alias && !alias_value) ||
            (has_primary && has_alias && primary_value != alias_value))
            return std::nullopt;
        return has_primary ? primary_value : alias_value;
    };
    const auto sid = read_alias("session_id", "sid", kMaximumSessionBytes);
    if (!sid) return false;
    std::optional<SdkStartGameParams> params;
    if (finished.contains("sdk_msg") || finished.contains("sdk_param")) {
        const auto encoded = read_alias("sdk_msg", "sdk_param", 87384);
        if (!encoded) return false;
        params = DecodeSdkStartGameParams(*encoded);
        if (!params || (!params->sid.empty() && params->sid != *sid)) return false;
    }
    result.sid = *sid;
    result.sdk_start_game_params = std::move(params);
    if (const auto provider = BoundedString(finished, "cloud_provider", 128))
        result.cloud_provider = *provider;
    result.status = DispatchStatus::Finished;
    result.requires_ticket_ack = ticket_ack;
    return true;
}

bool ReadQueue(const Json& data, DispatchResponse& result, bool initial) {
    if (!data.contains("queue_info") || !data["queue_info"].is_object())
        return false;
    const auto& info = data["queue_info"];
    if (!info.contains("query_interval")) return false;
    const auto interval = Integer(info["query_interval"]);
    if (!interval || *interval < 1 || *interval > 3600) return false;
    if (initial) {
        const auto ticket = BoundedString(info, "ticket", kMaximumTicketBytes);
        if (!ticket) return false;
        result.ticket = *ticket;
    }
    result.query_interval_seconds = *interval;
    result.queue_rank = OptionalQueueNumber(info,"queue_rank");
    result.queue_length = OptionalQueueNumber(info,"queue_length");
    result.branch_queue_length = OptionalQueueNumber(info,"branch_queue_len");
    result.status = DispatchStatus::Queued;
    return true;
}

std::optional<Json> ReadEnvelope(long http_status, const std::string& body,
                                 DispatchResponse& result) {
    result.http_status = http_status;
    if (http_status < 200 || http_status >= 300) {
        result.status = DispatchStatus::HttpError;
        return std::nullopt;
    }
    if (body.empty() || body.size() > kMaximumResponseBytes) return std::nullopt;
    const auto json = Json::parse(body, nullptr, false);
    if (!json.is_object() || !json.contains("retcode")) return std::nullopt;
    const auto retcode = Integer(json["retcode"]);
    if (!retcode) return std::nullopt;
    result.retcode = *retcode;
    if (*retcode != 0) {
        result.status = DispatchStatus::ApiRejected;
        return std::nullopt;
    }
    if (!json.contains("data") || !json["data"].is_object())
        return std::nullopt;
    return json["data"];
}

} // namespace

DispatchResponse ParseInitialResponse(long http_status, const std::string& body) {
    DispatchResponse result;
    const auto data = ReadEnvelope(http_status, body, result);
    if (!data) return result;
    const auto code = BoundedString(*data, "result_code", 64);
    if (!code) return result;
    if (*code == "FINISHED") ReadFinished(*data, result, false);
    else if (*code == "QUEUED") ReadQueue(*data, result, true);
    else if (*code == "UNAVAILABLE") result.status = DispatchStatus::Unavailable;
    else if (*code == "SWITCH_UNAVAILABLE") result.status = DispatchStatus::SwitchUnavailable;
    else if (*code == "MAINTENANCE") result.status = DispatchStatus::Maintenance;
    else if (*code == "BAD_NETWORK") result.status = DispatchStatus::BadNetwork;
    return result;
}

DispatchResponse ParseTicketResponse(long http_status, const std::string& body) {
    DispatchResponse result;
    const auto data = ReadEnvelope(http_status, body, result);
    if (!data) return result;
    const auto status = BoundedString(*data, "ticket_status", 64);
    if (!status) return result;
    if (*status == "SUCCESS") ReadFinished(*data, result, true);
    else if (*status == "QUEUEING") ReadQueue(*data, result, false);
    else if (*status == "FAILED") result.status = DispatchStatus::Failed;
    else if (*status == "GAME_MAINTENANCE") result.status = DispatchStatus::Maintenance;
    else if (*status == "DEVICE_CONFLICT") result.status = DispatchStatus::DeviceConflict;
    else if (*status == "OP_KICK_OUT") result.status = DispatchStatus::KickedOut;
    else if (*status == "TIMEOUT") result.status = DispatchStatus::TimedOut;
    return result;
}

} // namespace cloudgenshin::dispatch
