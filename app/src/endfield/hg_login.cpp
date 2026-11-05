#include "endfield/hg_login.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cctype>
#include <utility>

namespace cloudgenshin::endfield::login {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

std::string PercentEncode(std::string_view value) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const char raw : value) {
        const auto character = static_cast<unsigned char>(raw);
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            out.push_back(static_cast<char>(character));
        } else {
            out.push_back('%');
            out.push_back(hex[character >> 4]);
            out.push_back(hex[character & 0x0F]);
        }
    }
    return out;
}

Json ParseJson(const std::string& text) {
    return Json::parse(text, nullptr, false);
}

}  // namespace

std::string BuildGenScanLoginBody() {
    OrderedJson body;
    body["appCode"] = kPassportAppCode;
    body["enableRememberLogin"] = true;
    return body.dump();
}

std::string BuildTokenByScanCodeBody(std::string_view scan_code) {
    OrderedJson body;
    body["appCode"] = kPassportAppCode;
    body["from"] = 0;
    body["scanCode"] = scan_code;
    return body.dump();
}

std::string BuildScanStatusPath(std::string_view scan_id) {
    return "/general/v1/scan_status?scanId=" + PercentEncode(scan_id);
}

Envelope ParseEnvelope(std::string_view body) {
    Envelope envelope;
    const Json root = ParseJson(std::string(body));
    if (!root.is_object()) return envelope;

    const auto status = root.find("status");
    if (status == root.end() || !status->is_number_integer()) return envelope;
    envelope.parsed = true;
    envelope.status = status->get<long>();

    if (const auto message = root.find("msg");
        message != root.end() && message->is_string()) {
        envelope.message = message->get<std::string>();
    }

    if (const auto data = root.find("data");
        data != root.end() && !data->is_null()) {
        if (data->is_string()) {
            envelope.data_json = data->get<std::string>();
        } else {
            envelope.data_json = data->dump();
        }
    }
    return envelope;
}

std::optional<ScanSession> ParseGenScanLogin(const Envelope& envelope) {
    if (!envelope.parsed || envelope.status != 0) return std::nullopt;
    const Json data = ParseJson(envelope.data_json);
    if (!data.is_object()) return std::nullopt;

    const auto scan_id = data.find("scanId");
    const auto scan_url = data.find("scanUrl");
    if (scan_id == data.end() || !scan_id->is_string()) return std::nullopt;
    if (scan_url == data.end() || !scan_url->is_string()) return std::nullopt;

    ScanSession session;
    session.scan_id = scan_id->get<std::string>();
    session.scan_url = scan_url->get<std::string>();
    if (session.scan_id.empty() || session.scan_url.empty()) return std::nullopt;
    return session;
}

PollResult ParseScanStatus(const Envelope& envelope) {
    PollResult result;
    result.code = envelope.status;
    result.message = envelope.message;
    if (!envelope.parsed) return result;

    if (envelope.status == 0) {
        const Json data = ParseJson(envelope.data_json);
        if (data.is_object()) {
            const auto scan_code = data.find("scanCode");
            if (scan_code != data.end() && scan_code->is_string()) {
                result.scan_code = scan_code->get<std::string>();
                if (!result.scan_code.empty()) {
                    result.status = PollStatus::Authorized;
                    return result;
                }
            }
        }
        return result;
    }

    switch (envelope.status) {
        case 100: result.status = PollStatus::Waiting; break;
        case 101: result.status = PollStatus::Scanned; break;
        case 102: result.status = PollStatus::Expired; break;
        default: result.status = PollStatus::ApiError; break;
    }
    return result;
}

std::optional<TokenResult> ParseTokenByScanCode(const Envelope& envelope) {
    if (!envelope.parsed || envelope.status != 0) return std::nullopt;
    const Json data = ParseJson(envelope.data_json);
    if (!data.is_object()) return std::nullopt;

    const auto token = data.find("token");
    if (token == data.end() || !token->is_string()) return std::nullopt;

    TokenResult result;
    result.token = token->get<std::string>();
    if(const auto device=data.find("deviceToken");device!=data.end() && device->is_string())
        result.device_token=device->get<std::string>();
    // uid arrives as a numeric or textual id; both are stored verbatim.
    if (const auto uid = data.find("uid"); uid != data.end()) {
        if (uid->is_string())
            result.uid = uid->get<std::string>();
        else if (!uid->is_null() && uid->is_number())
            result.uid = uid->dump();
    }
    if (result.token.empty()) return std::nullopt;
    if (const auto remember = data.find("rememberLogin");
        remember != data.end() && remember->is_boolean()) {
        result.remember_login = remember->get<bool>();
    }
    return result;
}

}  // namespace cloudgenshin::endfield::login
