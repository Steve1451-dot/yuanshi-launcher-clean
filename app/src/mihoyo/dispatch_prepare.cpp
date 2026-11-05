#include "mihoyo/dispatch_prepare.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/queue_ticket.hpp"
#include "mihoyo/ping_probe.hpp"
#include <borealis/extern/nlohmann/json.hpp>

namespace cloudgenshin::dispatch {
namespace {
using Json = nlohmann::json;
bool Text(const Json& value, std::size_t limit) {
    if (!value.is_string()) return false;
    const auto& text = value.get_ref<const std::string&>();
    if (text.empty() || text.size() > limit) return false;
    for (unsigned char c : text) if (c < 0x20 || c == 0x7f) return false;
    return true;
}
const char* Path(PrepareOperation operation) {
    switch (operation) {
        case PrepareOperation::Status: return "statusCheck";
        case PrepareOperation::PingServers: return "listPingServer";
        case PrepareOperation::Nodes: return "getNodesInfo";
        case PrepareOperation::Verify: return "preDispatchVerify";
    }
    return nullptr;
}
bool Regions(const Json& regions) {
    if (!regions.is_array() || regions.size() > 64) return false;
    for (const auto& value : regions) if (!Text(value, 128)) return false;
    return true;
}
} // namespace

std::optional<PrepareRequest> BuildPrepareRequest(const GameProfile& profile,
    PrepareOperation operation,
    const auth::ComboLoginFields& fields, const auth::WebDeviceIdentity& device, const Node& node,
    const std::string& measured_ping_node) {
    const auto* path = Path(operation);
    const auto login = auth::BuildGamerLoginRequest(profile, fields, device);
    if (!path || !login) return std::nullopt;
    Json body{{"biz_key", std::string(profile.game_biz)}};
    if (operation == PrepareOperation::PingServers) body["ext_data"] = Json{{"platform",19}}.dump();
    if (operation == PrepareOperation::Nodes) {
        if(measured_ping_node.empty() || measured_ping_node.size()>64*1024) return std::nullopt;
        const auto ping=Json::parse(measured_ping_node,nullptr,false);
        if(!ping.is_object() || !ping.contains("ping_results") || !ping["ping_results"].is_array() ||
            ping["ping_results"].empty() || ping["ping_results"].size()>32 ||
            !ping.contains("bandwidth") || !ping["bandwidth"].is_number() || ping["bandwidth"]!=0) return std::nullopt;
        for(const auto& entry:ping["ping_results"]) {
            if(!entry.is_object() || !entry.contains("ping_server_id") || !entry["ping_server_id"].is_number_unsigned() ||
                !entry.contains("rtt") || !entry["rtt"].is_number() || entry["rtt"]<0 || entry["rtt"]>10000) return std::nullopt;
        }
        body["node"] = measured_ping_node; body["speed_client_type"] = 7;
    }
    if (operation == PrepareOperation::Verify) {
        if (!Text(Json(node.id),256) || node.regions_json.size() > 8192) return std::nullopt;
        const auto regions = Json::parse(node.regions_json,nullptr,false);
        if (!Regions(regions)) return std::nullopt;
        std::string signed_fields = "biz_key=" + std::string(profile.game_biz) + "&node_id=" + node.id;
        if (!regions.empty()) signed_fields += "&regions=" + regions.dump();
        const auto signature = auth::HmacSha256Hex(std::string(profile.dispatch_app_key), signed_fields);
        if (!signature) return std::nullopt;
        body["node_id"] = node.id; body["regions"] = regions; body["sign"] = *signature;
    }
    auto headers = login->headers;
    headers.push_back("Content-Type: application/json");
    return PrepareRequest{operation, std::string(profile.cloud_base)+"/dispatcher/api/"+path,
        login->user_agent,body.dump(),headers};
}

PrepareResult ParsePrepareResponse(PrepareOperation operation, long http_status, const std::string& body) {
    PrepareResult result;
    result.http_status = http_status;
    const auto envelope = ParseQueueActionResponse(http_status,body);
    result.retcode = envelope.retcode;
    if (envelope.status == QueueActionStatus::HttpError) { result.status=PrepareStatus::HttpError; return result; }
    if (envelope.status == QueueActionStatus::ApiRejected) { result.status=PrepareStatus::Rejected; return result; }
    if (envelope.status != QueueActionStatus::Success) return result;
    const auto json = Json::parse(body,nullptr,false);
    if (!json.contains("data") || !json["data"].is_object()) return result;
    const auto& data = json["data"];
    if (operation == PrepareOperation::Status || operation == PrepareOperation::Verify) {
        if (!data.contains("maintenance_info")) return result;
        result.status = data["maintenance_info"].is_null() ? PrepareStatus::Success : PrepareStatus::Maintenance;
    } else if (operation == PrepareOperation::PingServers) {
        if (!data.contains("ping_svr") || !data["ping_svr"].is_array() ||
            data["ping_svr"].size() > 128 || !data.contains("sdk_param") ||
            !data["sdk_param"].is_string() || data["sdk_param"].get_ref<const std::string&>().size() > 128*1024)
            return result;
        result.status = PrepareStatus::Success;
        result.ping_sdk_param=data["sdk_param"].get<std::string>();
    } else if (operation == PrepareOperation::Nodes) {
        if (!data.contains("result_code") || data["result_code"] != "NONE") {
            result.status=PrepareStatus::Unavailable; return result;
        }
        if (!data.contains("nodes") || !data["nodes"].is_array() || data["nodes"].size() > 64)
            return result;
        for (const auto& node : data["nodes"]) {
            if (!node.is_object() || !node.contains("node_id") || !Text(node["node_id"],256) ||
                !node.contains("region_ids") || !Regions(node["region_ids"]) ||
                !node.contains("recommend") || !node["recommend"].is_boolean()) return PrepareResult{};
            result.nodes.push_back({node["node_id"].get<std::string>(), node["region_ids"].dump(), node["recommend"].get<bool>()});
        }
        result.status = result.nodes.empty() ? PrepareStatus::Unavailable : PrepareStatus::Success;
    }
    return result;
}
} // namespace cloudgenshin::dispatch
