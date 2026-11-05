#include "endfield/hg_api.hpp"

#include "endfield/endfield_profile.hpp"
#include "endfield/jwe.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>

namespace cloudgenshin::endfield::api {
namespace {

using Json = nlohmann::json;
using OrderedJson = nlohmann::ordered_json;

// Web gameManager constructor default (tF(this,"gameVersion","v1.0")).
constexpr std::string_view kGameVersion = "v1.0";

// Official cmd_line template (bundle module 96042; identical in the staging
// ENDFIELD/DEMO environments). Placeholders are replaced in order of
// appearance by [cloudPlatform, "WxH", "LxTxRxB", true].
constexpr std::string_view kCommandLineTemplate =
    "-cloud_platform=%d -cloud_display_resolution=%s -cloud_safe_area=%s -cloud_is_web=%b";

long long JsonIntField(const Json& object, const char* key, long long fallback) {
    const auto iterator = object.find(key);
    if (iterator == object.end() || !iterator->is_number()) return fallback;
    if (iterator->is_number_integer() || iterator->is_number_unsigned())
        return iterator->get<long long>();
    return static_cast<long long>(iterator->get<double>());
}

std::string JsonStringField(const Json& object, const char* key) {
    const auto iterator = object.find(key);
    if (iterator == object.end()) return {};
    if (iterator->is_string()) return iterator->get<std::string>();
    if (iterator->is_number_integer() || iterator->is_number_unsigned())
        return std::to_string(iterator->get<long long>());
    return {};
}

bool LooksLikeCompactJwe(const std::string& text) {
    if (text.size() < 64 || text.size() > 65536) return false;
    std::size_t dots = 0;
    for (const char c : text) {
        if (c == '.') {
            ++dots;
        } else if (!(c == '-' || c == '_' || c == '=' || c == '+' || c == '/' ||
                     (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') ||
                     (c >= 'A' && c <= 'Z'))) {
            return false;
        }
    }
    return dots == 4;
}

void MergeTenantField(const Json& tenant, const char* key, std::string& target) {
    const std::string value = JsonStringField(tenant, key);
    if (!value.empty()) target = value;
}

}  // namespace

std::string BuildExchangeBody(const std::string& hg_token, const std::string& u8_token,
                              const std::string& public_jwk) {
    // Web insertion order: {hg_token, u8_token} then epk_jwk appended (t7).
    // The server rejects an empty u8_token with code 11204 ("need u8_token"),
    // so both app-granted and U8 credentials are supplied (FetchCloudCredentials).
    OrderedJson body;
    body["hg_token"] = hg_token;
    body["u8_token"] = u8_token;
    body["epk_jwk"] = public_jwk;
    return body.dump();
}

std::string BuildDispatchBody(const DispatchInputs& inputs) {
    // Web object literal order (module 811xxx).
    OrderedJson body;
    body["tenant_key"] = inputs.tenant_key;
    body["node"] = inputs.node_id;
    body["gameId"] = inputs.game_id;
    body["resolution"] = inputs.resolution;
    body["bit_rate"] = inputs.bit_rate;
    body["fps"] = inputs.fps==30 || inputs.fps==60 ? inputs.fps : kDispatchFps;
    body["codec_type"] = inputs.codec_type;
    body["cmd_line"] = inputs.cmd_line.empty() ? BuildCommandLine(inputs.resolution, kDispatchCloudPlatform, kDispatchIsWeb)
                                               : inputs.cmd_line;
    body["pay_type"] = kPayTypeNormal;
    body["version"] = std::string(kGameVersion);
    body["biz_data"] = BuildBizData();
    return body.dump();
}

std::string BuildCancelBody(const std::string& session_id) {
    OrderedJson body;
    body["session_id"] = session_id;
    return body.dump();
}

std::string BuildNodeListBody(const std::vector<DeployNode>& nodes) {
    OrderedJson list = OrderedJson::array();
    for (const auto& node : nodes) {
        OrderedJson entry;
        entry["node_id"] = node.node_id;
        entry["node_name"] = node.node_name;
        entry["delay"] = node.delay;
        list.push_back(std::move(entry));
    }
    OrderedJson body;
    body["node_list"] = std::move(list);
    return body.dump();
}

std::string BuildCommandLine(const std::string& resolution, int cloud_platform, bool is_web) {
    const std::string values[4] = {std::to_string(cloud_platform), resolution, "0x0x0x0",
                                   is_web ? "true" : "false"};
    std::string out;
    out.reserve(kCommandLineTemplate.size() + resolution.size() + 24);
    std::size_t index = 0;
    for (std::size_t i = 0; i < kCommandLineTemplate.size(); ++i) {
        const char c = kCommandLineTemplate[i];
        if (c == '%' && i + 1 < kCommandLineTemplate.size()) {
            const char kind = kCommandLineTemplate[i + 1];
            if (kind == 'd' || kind == 's' || kind == 'b') {
                if (index < 4) out += values[index];
                ++index;
                ++i;
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

std::string BuildBizData() {
    // SDK getBizData() shape (findings §14.5). os/osVer are descriptive
    // metadata for the pod; the Switch port presents as Windows so the pod
    // keeps Windows input/cursor semantics.
    OrderedJson biz;
    biz["btype"] = "";
    biz["os"] = std::string(kOsName);
    biz["osVer"] = "10.0";
    biz["clientVer"] = std::string(kWeLinkSdkVersion);
    biz["osCategory"] = "H5";
    biz["isOneLine"] = 1;
    biz["extSDK"] = "{\"certHash\":true}";
    return biz.dump();
}

std::optional<InstanceKey> ParseInstanceKey(std::string_view tenant_params) {
    // Web dO(): instanceId = params without the last 6 chars; gameId = last
    // 6 chars with leading zeros stripped.
    if (tenant_params.size() <= 6 || tenant_params.size() > 512) return std::nullopt;
    InstanceKey key;
    key.instance_id = std::string(tenant_params.substr(0, tenant_params.size() - 6));
    key.game_id = std::string(tenant_params.substr(tenant_params.size() - 6));
    const auto first = key.game_id.find_first_not_of('0');
    key.game_id = first == std::string::npos ? std::string() : key.game_id.substr(first);
    if (key.instance_id.empty() || key.game_id.empty()) return std::nullopt;
    return key;
}

bool ParseDeployNodeList(const std::string& body, std::vector<DeployNode>* out) {
    if (!out) return false;
    const Json root = Json::parse(body, nullptr, false);
    if (!root.is_object()) return false;
    auto list = root.find("lines");
    if(list==root.end()) list=root.find("nodeList");
    if (list == root.end() || !list->is_array()) return false;
    out->clear();
    for (const auto& entry : *list) {
        if (!entry.is_object()) continue;
        DeployNode node;
        node.node_id = JsonStringField(entry, "nodeId");
        node.node_name = JsonStringField(entry, "nodeName");
        if (node.node_id.empty()) continue;
        out->push_back(std::move(node));
        if (out->size() >= 64) break;
    }
    return !out->empty();
}

bool ParseNodeListData(const std::string& data_json, std::vector<DeployNode>* out) {
    if (!out) return false;
    const Json data = Json::parse(data_json, nullptr, false);
    if (!data.is_object()) return false;
    const auto list = data.find("node_list");
    if (list == data.end() || !list->is_array()) return false;
    out->clear();
    for (const auto& entry : *list) {
        if (!entry.is_object()) continue;
        DeployNode node;
        node.node_id = JsonStringField(entry, "node_id");
        node.node_name = JsonStringField(entry, "node_name");
        node.delay = JsonIntField(entry, "delay", 0);
        node.state = JsonIntField(entry, "state", 0);
        node.normal_wait_time = JsonIntField(entry, "normal_wait_time", 0);
        node.quick_wait_time = JsonIntField(entry, "quick_wait_time", 0);
        node.trial_card_wait_time = JsonIntField(entry, "trial_card_wait_time", 0);
        node.play_card_wait_time = JsonIntField(entry, "play_card_wait_time", 0);
        if (node.node_id.empty()) continue;
        out->push_back(std::move(node));
        if (out->size() >= 64) break;
    }
    return !out->empty();
}

Envelope ParseEnvelope(const std::string& body) {
    Envelope envelope;
    const Json root = Json::parse(body, nullptr, false);
    if (!root.is_object()) return envelope;
    envelope.parsed = true;
    const auto code_value=[](const Json& value)->std::optional<long> {
        if(!value.is_number_integer()) return std::nullopt;
        if(value.is_number_unsigned()) {
            const auto code=value.get<std::uint64_t>();
            if(code>static_cast<std::uint64_t>(std::numeric_limits<long>::max())) return std::nullopt;
            return static_cast<long>(code);
        }
        const auto code=value.get<std::int64_t>();
        if(code<std::numeric_limits<long>::min() || code>std::numeric_limits<long>::max()) return std::nullopt;
        return static_cast<long>(code);
    };
    std::optional<long> code;
    bool code_present=false;
    const auto error = root.find("error");
    if (error != root.end() && error->is_object()) {
        code_present=true;
        const auto field=error->find("code");
        if(field!=error->end()) code=code_value(*field);
        envelope.message = JsonStringField(*error, "msg");
    } else {
        // Actual HTTP payloads use {code:0,data:...}; isSuccess/error is
        // normally added by the web client's request wrapper, not the server.
        const auto field=root.find("code");
        if(field!=root.end()) {code_present=true;code=code_value(*field);}
        envelope.message = JsonStringField(root, "msg");
    }
    const auto success=root.find("isSuccess");
    if(success!=root.end())
        envelope.success=success->is_boolean() && success->get<bool>() &&
                         (!code_present || (code && *code==0));
    else
        envelope.success=code && *code==0 && (error==root.end() || !error->is_object());
    envelope.code=code.value_or(envelope.success ? 0 : -1);
    if(!envelope.success && envelope.code==0) envelope.code=-1;

    if (const auto timestamp = root.find("timestamp");
        timestamp != root.end() && timestamp->is_number())
        envelope.timestamp = JsonIntField(root, "timestamp", 0);

    const auto data = root.find("data");
    if (data != root.end() && !data->is_null()) {
        envelope.data_json = data->dump();
        if (envelope.timestamp == 0)
            envelope.timestamp = JsonIntField(*data, "timestamp", 0);
    }
    return envelope;
}

std::optional<LoginSession> ParseLoginSessionData(const std::string& data_json,
                                                  const EcdhP256* key,
                                                  std::string* decrypt_diagnostic) {
    const Json data = Json::parse(data_json, nullptr, false);
    if (!data.is_object()) return std::nullopt;

    LoginSession session;
    session.login_session_id = JsonStringField(data, "login_session_id");
    session.server_id = JsonStringField(data, "server_id");
    session.tenant_key = JsonStringField(data, "tenant_key");
    session.host_url = JsonStringField(data, "host_url");
    session.game_id = JsonStringField(data, "game_id");
    session.tenant_params = JsonStringField(data, "tenant_params");

    // The tenant data arrives either as vlink_tenant_data or, when that field
    // is absent, as a compact JWE in tenant_params itself.
    std::string jwe_text = JsonStringField(data, "vlink_tenant_data");
    if (jwe_text.empty() && LooksLikeCompactJwe(session.tenant_params))
        jwe_text = session.tenant_params;

    if (key && !jwe_text.empty()) {
        std::string diagnostic;
        const auto plaintext = key->DecryptTenantData(jwe_text, &diagnostic);
        if (decrypt_diagnostic) *decrypt_diagnostic = diagnostic;
        const Json tenant = plaintext ? Json::parse(*plaintext, nullptr, false) : Json();
        if (plaintext && tenant.is_object()) {
            MergeTenantField(tenant, "tenant_params", session.tenant_params);
            MergeTenantField(tenant, "tenant_key", session.tenant_key);
            MergeTenantField(tenant, "host_url", session.host_url);
            MergeTenantField(tenant, "game_id", session.game_id);
        } else if (session.host_url.empty() || session.tenant_key.empty()) {
            // Without decrypted tenant data there is nothing to dispatch to.
            return std::nullopt;
        }
    }
    if (session.login_session_id.empty() || session.tenant_key.empty() ||
        session.host_url.empty())
        return std::nullopt;
    return session;
}

std::optional<QueueState> ParseQueueState(const std::string& data_json) {
    const Json data = Json::parse(data_json, nullptr, false);
    if (!data.is_object()) return std::nullopt;
    QueueState state;
    state.session_id = JsonStringField(data, "session_id");
    state.status = JsonIntField(data, "status", 0);
    state.queuing_size = JsonIntField(data, "queuing_size", 0);
    state.queuing_seat_no = JsonIntField(data, "queuing_seat_no", 0);
    state.estimated_wait_time = JsonIntField(data, "estimated_wait_time", 0);
    state.user_level = JsonIntField(data, "user_level", 0);
    if (const auto sdk = data.find("sdk_msg"); sdk != data.end() && !sdk->is_null()) {
        if (sdk->is_string())
            state.sdk_msg = sdk->get<std::string>();
        else
            state.sdk_msg = sdk->dump();
    }
    // The pod-assigned payload carries the game session under a different key;
    // accept the known spellings so the release path can target it (top level
    // first, then inside sdk_msg).
    const auto pick_game_session = [](const Json& value) -> std::string {
        if (!value.is_object()) return {};
        for (const char* key : {"gs_session_id", "game_session_id", "gsSessionId",
                                "game_session", "running_session_id"}) {
            const std::string found = JsonStringField(value, key);
            if (!found.empty()) return found;
        }
        return {};
    };
    state.game_session_id = pick_game_session(data);
    if (state.game_session_id.empty() && !state.sdk_msg.empty())
        state.game_session_id = pick_game_session(Json::parse(state.sdk_msg, nullptr, false));
    return state;
}

}  // namespace cloudgenshin::endfield::api
