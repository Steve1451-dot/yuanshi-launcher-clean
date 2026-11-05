#include "mihoyo/rtc_signal.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <utility>

namespace cloudgenshin::rtc {
namespace {

constexpr std::size_t kMaximumSignalBytes = 256 * 1024;
constexpr std::size_t kMaximumCandidateBytes = 16 * 1024;

} // namespace

std::optional<SignalMessage> ParseSignalMessage(const std::vector<std::uint8_t>& payload) {
    if (payload.empty() || payload.size() > kMaximumSignalBytes) return std::nullopt;
    const auto json = nlohmann::json::parse(payload.begin(), payload.end(), nullptr, false);
    if (!json.is_object() || !json.contains("type") || !json["type"].is_string())
        return std::nullopt;

    const auto type = json["type"].get<std::string>();
    if (type == "offer" || type == "answer") {
        if (!json.contains("sdp") || !json["sdp"].is_string()) return std::nullopt;
        auto sdp = json["sdp"].get<std::string>();
        if (sdp.empty() || sdp.size() > kMaximumSignalBytes) return std::nullopt;
        return SignalMessage{type == "offer" ? SignalType::Offer : SignalType::Answer,
                             std::move(sdp), {}, {}, -1};
    }
    if (type == "candidates") {
        if (!json.contains("candidate") || !json["candidate"].is_string())
            return std::nullopt;
        auto candidate = json["candidate"].get<std::string>();
        if (candidate.empty() || candidate.size() > kMaximumCandidateBytes)
            return std::nullopt;
        std::string mid;
        if (json.contains("sdp-mid") && !json["sdp-mid"].is_null()) {
            if (!json["sdp-mid"].is_string()) return std::nullopt;
            mid = json["sdp-mid"].get<std::string>();
            if (mid.size() > 64) return std::nullopt;
        }
        int index = -1;
        if (json.contains("sdp-mline-index") && !json["sdp-mline-index"].is_null()) {
            const auto& value = json["sdp-mline-index"];
            if (!value.is_number_integer() || value < 0 || value > 255)
                return std::nullopt;
            index = value.get<int>();
        }
        return SignalMessage{SignalType::Candidate, {}, std::move(candidate),
                             std::move(mid), index};
    }
    return std::nullopt;
}

std::optional<std::vector<std::uint8_t>> EncodeAnswer(const std::string& sdp) {
    if (sdp.empty() || sdp.size() > kMaximumSignalBytes) return std::nullopt;
    const auto json = nlohmann::json{{"type", "answer"}, {"sdp", sdp}}.dump();
    if (json.size() > kMaximumSignalBytes) return std::nullopt;
    return std::vector<std::uint8_t>(json.begin(), json.end());
}

std::optional<std::vector<std::uint8_t>> EncodeCandidate(
    const std::string& candidate, const std::string& sdp_mid, int sdp_mline_index) {
    if (candidate.empty() || candidate.size() > kMaximumCandidateBytes ||
        sdp_mid.size() > 64 || sdp_mline_index < 0 || sdp_mline_index > 255)
        return std::nullopt;
    const auto json = nlohmann::json{{"type", "candidates"},
                                     {"candidate", candidate},
                                     {"sdp-mid", sdp_mid},
                                     {"sdp-mline-index", sdp_mline_index}}.dump();
    return std::vector<std::uint8_t>(json.begin(), json.end());
}

} // namespace cloudgenshin::rtc
