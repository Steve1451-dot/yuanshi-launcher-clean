#include "mihoyo/dispatch_response.hpp"
#include "mihoyo_sdk_start_game_fixture.hpp"

#include <borealis/extern/nlohmann/json.hpp>

#include <cassert>
#include <string>

int main() {
    using namespace cloudgenshin::dispatch;

    const auto finished = ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"FINISHED","finish_result":{"sid":"synthetic-session","cloud_provider":"test"}}})");
    assert(finished.status == DispatchStatus::Finished);
    assert(finished.sid == "synthetic-session");
    assert(finished.cloud_provider == "test");
    assert(!finished.requires_ticket_ack);
    assert(!finished.sdk_start_game_params); // Metadata alone is not launchable.

    nlohmann::json native_finished = {{"retcode", 0}, {"data", {
        {"result_code", "FINISHED"}, {"finish_result", {
            {"session_id", "synthetic-session"}, {"sdk_msg", kSyntheticSdkStartGameBase64}
        }}
    }}};
    auto native = ParseInitialResponse(200, native_finished.dump());
    assert(native.status == DispatchStatus::Finished && native.sdk_start_game_params);
    assert(native.sdk_start_game_params->game_server_wss_url == "wss://signal.example.invalid/rtc");
    auto& native_fields = native_finished["data"]["finish_result"];
    native_fields["sid"] = native_fields["session_id"];
    native_fields["sdk_param"] = native_fields["sdk_msg"];
    assert(ParseInitialResponse(200, native_finished.dump()).sdk_start_game_params);
    native_fields.erase("session_id");
    native_fields.erase("sdk_msg");
    assert(ParseInitialResponse(200, native_finished.dump()).sdk_start_game_params);
    native_finished["data"].erase("result_code");
    native_finished["data"]["ticket_status"] = "SUCCESS";
    native = ParseTicketResponse(200, native_finished.dump());
    assert(native.status == DispatchStatus::Finished && native.requires_ticket_ack);
    assert(native.sdk_start_game_params->pod_id == "synthetic-pod");
    native_fields["sid"] = "different-session";
    native = ParseTicketResponse(200, native_finished.dump());
    assert(native.status == DispatchStatus::InvalidResponse && native.sid.empty());
    assert(!native.sdk_start_game_params && !native.requires_ticket_ack);
    native_fields["sid"] = "synthetic-session";
    native_fields["session_id"] = "conflicting-session";
    assert(ParseTicketResponse(200, native_finished.dump()).status == DispatchStatus::InvalidResponse);
    native_fields.erase("session_id");
    native_fields["sdk_msg"] = "CgF4";
    assert(ParseTicketResponse(200, native_finished.dump()).status == DispatchStatus::InvalidResponse);
    native_fields.erase("sdk_msg");
    for (const nlohmann::json& bad : {nlohmann::json("not-base64"), nlohmann::json(""),
                                    nlohmann::json(nullptr), nlohmann::json(12)}) {
        native_fields["sdk_param"] = bad;
        assert(ParseTicketResponse(200, native_finished.dump()).status == DispatchStatus::InvalidResponse);
    }

    const auto queued = ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"QUEUED","queue_info":{"ticket":"synthetic-ticket","query_interval":5,"branch_queue_len":"19"}}})");
    assert(queued.status == DispatchStatus::Queued);
    assert(queued.ticket == "synthetic-ticket");
    assert(queued.query_interval_seconds == 5);
    assert(queued.branch_queue_length == 19);
    assert(!queued.queue_rank && !queued.queue_length);

    auto queue_fields=nlohmann::json{{"retcode",0},{"data",{
        {"ticket_status","QUEUEING"},{"queue_info",{
            {"query_interval",3},{"branch_queue_len",439},
            {"queue_length","480"},{"queue_rank","2"}
        }}
    }}};
    auto numbers=ParseTicketResponse(200,queue_fields.dump());
    assert(numbers.status==DispatchStatus::Queued && numbers.queue_rank==2);
    assert(numbers.queue_length==480 && numbers.branch_queue_length==439);
    for(const char* field:{"queue_rank","queue_length","branch_queue_len"}) {
        for(const nlohmann::json& bad:{nlohmann::json(-1),nlohmann::json("-1"),
            nlohmann::json(true),nlohmann::json(1.5),nlohmann::json(nullptr),
            nlohmann::json(""),nlohmann::json(" 2"),nlohmann::json("2x"),
            nlohmann::json("2147483648"),nlohmann::json(2147483648ULL),
            nlohmann::json("99999999999")}) {
            auto invalid=queue_fields;
            invalid["data"]["queue_info"][field]=bad;
            const auto parsed=ParseTicketResponse(200,invalid.dump());
            assert(parsed.status==DispatchStatus::Queued); // optional metadata
            if(std::string(field)=="queue_rank") assert(!parsed.queue_rank);
            if(std::string(field)=="queue_length") assert(!parsed.queue_length);
            if(std::string(field)=="branch_queue_len") assert(!parsed.branch_queue_length);
        }
        for(const nlohmann::json& valid:{nlohmann::json(0),nlohmann::json("0"),
            nlohmann::json(2147483647),nlohmann::json("2147483647")}) {
            auto edge=queue_fields;
            edge["data"]["queue_info"][field]=valid;
            const auto parsed=ParseTicketResponse(200,edge.dump());
            if(std::string(field)=="queue_rank") assert(parsed.queue_rank.has_value());
            if(std::string(field)=="queue_length") assert(parsed.queue_length.has_value());
            if(std::string(field)=="branch_queue_len") assert(parsed.branch_queue_length.has_value());
        }
    }

    const auto polling = ParseTicketResponse(200,
        R"({"retcode":0,"data":{"ticket_status":"QUEUEING","queue_info":{"query_interval":3,"branch_queue_len":7}}})");
    assert(polling.status == DispatchStatus::Queued);
    assert(polling.ticket.empty());
    assert(polling.query_interval_seconds == 3);
    assert(polling.branch_queue_length == 7);

    const auto success = ParseTicketResponse(200,
        R"({"retcode":0,"data":{"ticket_status":"SUCCESS","finish_result":{"sid":"synthetic-session"}}})");
    assert(success.status == DispatchStatus::Finished && success.requires_ticket_ack);
    assert(success.sid == "synthetic-session");

    assert(ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"SWITCH_UNAVAILABLE"}})").status ==
        DispatchStatus::SwitchUnavailable);
    assert(ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"BAD_NETWORK"}})").status ==
        DispatchStatus::BadNetwork);
    assert(ParseTicketResponse(200,
        R"({"retcode":0,"data":{"ticket_status":"DEVICE_CONFLICT"}})").status ==
        DispatchStatus::DeviceConflict);
    assert(ParseTicketResponse(200,
        R"({"retcode":0,"data":{"ticket_status":"TIMEOUT"}})").status ==
        DispatchStatus::TimedOut);

    const auto rejected = ParseInitialResponse(200, R"({"retcode":-100,"message":"no"})");
    assert(rejected.status == DispatchStatus::ApiRejected && rejected.retcode == -100);
    assert(ParseInitialResponse(429, "{}").status == DispatchStatus::HttpError);
    assert(ParseInitialResponse(200, "not json").status == DispatchStatus::InvalidResponse);
    assert(ParseInitialResponse(200,
        R"({"retcode":18446744073709551615})").status == DispatchStatus::InvalidResponse);
    assert(ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"QUEUED","queue_info":{"ticket":"x","query_interval":0}}})").status ==
        DispatchStatus::InvalidResponse);
    assert(ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"QUEUED","queue_info":{"ticket":"x","query_interval":3601}}})").status ==
        DispatchStatus::InvalidResponse);
    assert(ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"QUEUED","queue_info":{"ticket":3,"query_interval":5}}})").status ==
        DispatchStatus::InvalidResponse);
    assert(ParseInitialResponse(200,
        R"({"retcode":0,"data":{"result_code":"QUEUED","queue_info":{"ticket":"x\nInjected: yes","query_interval":5}}})").status ==
        DispatchStatus::InvalidResponse);
    assert(ParseTicketResponse(200,
        R"({"retcode":0,"data":{"ticket_status":"SUCCESS","finish_result":{}}})").status ==
        DispatchStatus::InvalidResponse);
    assert(ParseTicketResponse(200,
        R"({"retcode":0,"data":{"ticket_status":"UNKNOWN"}})").status ==
        DispatchStatus::InvalidResponse);
    assert(ParseInitialResponse(200, std::string(1024 * 1024 + 1, 'x')).status ==
           DispatchStatus::InvalidResponse);
}
