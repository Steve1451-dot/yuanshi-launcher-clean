#include "endfield/hg_api.hpp"
#include "endfield/hg_login.hpp"
#include "http_client.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <cassert>
#include <functional>
#include <algorithm>

namespace {
std::atomic_bool cancelled{false};
const std::atomic_bool* expected_cancel=&cancelled;
long expected_timeout=15000;
std::vector<opennow::HttpResponse> responses;
std::vector<std::string> paths;
std::vector<std::string> bodies;
std::function<void()> after_request;
int transport_error_at=-1;
void Reset(std::vector<opennow::HttpResponse> replies) {
    cancelled=false;expected_cancel=&cancelled;expected_timeout=15000;
    responses=std::move(replies);paths.clear();bodies.clear();after_request={};transport_error_at=-1;
}
}

opennow::HttpResponse opennow::HttpClient::Request(const std::string&,const std::string& url,
    const std::string&,const std::vector<std::string>& headers,const std::string& body,
    const std::string&,opennow::HttpTransferControl control) const {
    assert(control.cancelled==expected_cancel && control.timeout_ms==
        (url.find("/ping/")!=std::string::npos ? 3000 : expected_timeout));
    if(url=="https://u8.hypergryph.com/u8/user/auth/v2/token_by_channel_token" ||
       url.starts_with("https://cloud-game.hypergryph.com/") || url.find("/ping/")!=std::string::npos) {
        assert(std::find(headers.begin(),headers.end(),"Accept-Encoding: identity")!=headers.end());
        assert(std::find(headers.begin(),headers.end(),"Connection: close")!=headers.end());
    }
    if(url.starts_with("https://cloud-game.hypergryph.com/")) {
        assert(std::find(headers.begin(),headers.end(),"X-Client-Platform: web")!=headers.end());
        assert(std::find(headers.begin(),headers.end(),"X-Client-Device-Model: Nintendo Switch")!=headers.end());
    }
    assert(paths.size()<responses.size());
    const auto reply=responses[paths.size()];paths.push_back(url);
    bodies.push_back(body);
    if(static_cast<int>(paths.size())==transport_error_at)
        throw opennow::HttpRequestError(CURLE_OPERATION_TIMEDOUT,0,"synthetic private context");
    if(after_request) after_request();
    return reply; // Synthetic requests only; never opens a connection.
}

int main() {
    using namespace cloudgenshin::endfield::api;
    const auto qr=cloudgenshin::endfield::login::ParseTokenByScanCode(
        cloudgenshin::endfield::login::ParseEnvelope(R"({"status":0,"data":{"token":"synthetic-passport","deviceToken":"synthetic-device-proof","rememberLogin":true}})"));
    assert(qr && qr->device_token=="synthetic-device-proof" && qr->remember_login);
    Reset({{200,R"({"status":0,"data":{"code":"synthetic-oauth-code","token":"different-app-token"}})"}});
    auto game_auth=cloudgenshin::endfield::login::AuthorizeGame("synthetic-passport","synthetic-device",&cancelled);
    assert(game_auth.status==0 && game_auth.code=="synthetic-oauth-code");
    auto game_body=nlohmann::json::parse(bodies[0]);
    assert(game_body["type"]==0 && game_body["deviceToken"]=="synthetic-device");
    Reset({{200,R"({"status":109,"data":{}})"}});
    game_auth=cloudgenshin::endfield::login::AuthorizeGame("synthetic-passport","",&cancelled);
    assert(game_auth.status==109 && game_auth.http==200 && game_auth.code.empty());
    assert(!nlohmann::json::parse(bodies[0]).contains("deviceToken"));
    Reset({{200,R"({"status":0,"data":{"token":"not-an-authorization-code"}})"}});
    assert(cloudgenshin::endfield::login::AuthorizeGame("synthetic-passport","",&cancelled).code.empty());
    assert(ParseEnvelope(R"({"code":0,"msg":"","data":{"session_id":"synthetic"}})").success);
    assert(ParseEnvelope(R"({"code":0,"data":null})").success);
    assert(!ParseEnvelope(R"({"data":{"session_id":"synthetic"}})").success);
    assert(!ParseEnvelope(R"({"code":"0","data":{}})").success);
    assert(!ParseEnvelope(R"({"code":18446744073709551615,"data":{}})").success);
    assert(!ParseEnvelope(R"({"isSuccess":false,"code":0,"data":{}})").success);
    assert(!ParseEnvelope(R"({"isSuccess":true,"error":{"code":12006}})").success);
    assert(ParseEnvelope(R"({"code":11204})").code==11204);
    const auto credential_replies=[] {
        return std::vector<opennow::HttpResponse>{
            {200,R"({"status":0,"data":{"token":"synthetic-cloud-grant"}})"},
            {200,R"({"status":0,"data":{"token":"synthetic-game-ticket"}})"}};
    };
    Reset(credential_replies());
    const auto tickets=FetchCloudCredentials("synthetic-passport",&cancelled);
    assert(tickets.credentials && tickets.credentials->hg_token=="synthetic-cloud-grant" &&
           tickets.credentials->u8_token=="synthetic-game-ticket" && tickets.error.code==0);
    assert(paths.size()==2 && paths[0]=="https://as.hypergryph.com/user/oauth2/v2/grant" &&
           paths[1]=="https://u8.hypergryph.com/u8/user/auth/v2/token_by_channel_token");
    const auto grant=nlohmann::json::parse(bodies[0]);
    const auto ticket=nlohmann::json::parse(bodies[1]);
    assert(grant["token"]=="synthetic-passport" && grant["appCode"]=="dd7b852d5f1dd9da" && grant["type"]==1);
    assert(ticket["appCode"]=="4df8f5a7c2ad711b497a" && ticket["channelMasterId"]==1 &&
           ticket["channelToken"]["type"]==0 && ticket["channelToken"]["token"]=="synthetic-passport");
    Reset({{200,R"({"status":0,"data":{"token":"synthetic-client-token","code":"synthetic-linked-code"}})"},
           {200,R"({"status":0,"data":{"token":"synthetic-linked-u8"}})"}});
    const auto linked=FetchCloudCredentials("synthetic-passport",&cancelled,"synthetic-device-proof");
    assert(linked.credentials && linked.credentials->hg_token=="synthetic-client-token" &&
           linked.credentials->u8_token=="synthetic-linked-u8" && linked.credentials->game_auth_code=="synthetic-linked-code");
    const auto linked_grant=nlohmann::json::parse(bodies[0]);
    const auto linked_ticket=nlohmann::json::parse(bodies[1]);
    assert(linked_grant["type"]==0 && linked_grant["deviceToken"]=="synthetic-device-proof");
    assert(linked_ticket["channelToken"]["type"]==1 && linked_ticket["channelToken"]["code"]==linked.credentials->game_auth_code);
    assert(!linked_ticket["channelToken"].contains("token") && linked_ticket["platform"]==7 && linked_ticket["type"]==0);
    Reset({{200,R"({"status":0,"data":{"token":"synthetic-client-token"}})"}});
    assert(!FetchCloudCredentials("synthetic-passport",&cancelled,"synthetic-device-proof").credentials && paths.size()==1);
    Reset({{200,R"({"status":109,"data":{}})"}});
    const auto proof_rejected=FetchCloudCredentials("synthetic-passport",&cancelled,"synthetic-device-proof");
    assert(!proof_rejected.credentials && proof_rejected.error.code==109 && paths.size()==1); // No fallback bypasses a device check.
    Reset({});cancelled=true;
    assert(!FetchCloudCredentials("synthetic-passport",&cancelled).credentials && paths.empty());
    Reset(credential_replies());after_request=[]{cancelled=true;};
    assert(!FetchCloudCredentials("synthetic-passport",&cancelled).credentials && paths.size()==1);
    Reset({{200,R"({"status":109,"data":{}})"}});
    const auto rejected=FetchCloudCredentials("synthetic-passport",&cancelled);
    assert(!rejected.credentials && rejected.stage==CloudCredentialsResult::Stage::Grant &&
           rejected.error.code==109 && rejected.error.http==200 && paths.size()==1);
    auto invalid_ticket=credential_replies();
    invalid_ticket[1]={200,R"({"status":3,"data":{}})"};Reset(std::move(invalid_ticket));
    const auto expired=FetchCloudCredentials("synthetic-passport",&cancelled);
    assert(!expired.credentials && expired.stage==CloudCredentialsResult::Stage::GameTicket &&
           expired.error.code==3 && expired.error.http==200 && paths.size()==2);
    auto missing_ticket=credential_replies();missing_ticket[1]={200,R"({"status":0,"data":{}})"};
    Reset(std::move(missing_ticket));
    assert(!FetchCloudCredentials("synthetic-passport",&cancelled).credentials && paths.size()==2);
    Reset(credential_replies());transport_error_at=2;
    const auto timeout=FetchCloudCredentials("synthetic-passport",&cancelled);
    assert(!timeout.credentials && timeout.stage==CloudCredentialsResult::Stage::GameTicket &&
           timeout.error.code==-1028 && timeout.error.message=="account transport failed");
    Reset({{200,R"({"status":0,"data":{"token":"synthetic-grant"}})"},
           {200,R"({"status":0,"data":{"token":"synthetic-u8"}})"}});
    assert(FetchU8Token("synthetic-passport","synthetic-uid",&cancelled).token=="synthetic-u8");
    assert(paths.size()==2 && paths[1].ends_with("/u8_token_by_uid"));
    Reset({{200,R"({"status":0,"data":{"token":"synthetic-grant"}})"}});
    after_request=[]{cancelled=true;};
    assert(!FetchU8Token("synthetic-passport","synthetic-uid",&cancelled).token && paths.size()==1);

    DispatchInputs inputs;inputs.tenant_key="synthetic-tenant";inputs.game_id="opaque-game+/=";
    assert(nlohmann::json::parse(BuildDispatchBody(inputs))["fps"]==60);
    assert(nlohmann::json::parse(BuildDispatchBody(inputs))["bit_rate"]==12000);
    inputs.fps=60;assert(nlohmann::json::parse(BuildDispatchBody(inputs))["fps"]==60);
    inputs.fps=120;assert(nlohmann::json::parse(BuildDispatchBody(inputs))["fps"]==60);
    inputs.fps=30;
    inputs.bit_rate=4000;
    assert(nlohmann::json::parse(BuildDispatchBody(inputs))["fps"]==30); // Explicit choice still works.
    assert(nlohmann::json::parse(BuildDispatchBody(inputs))["bit_rate"]==4000);
    const auto placeholder=ParseQueueState(R"({"status":1,"queuing_seat_no":28,"sdk_msg":{}})");
    assert(placeholder && !placeholder->sdk_msg.empty() && !placeholder->pod_assigned());
    const auto queued_string=ParseQueueState(R"({"status":1,"sdk_msg":"{}"})");
    assert(queued_string && !queued_string->pod_assigned());
    const auto assigned_state=ParseQueueState(R"({"status":2,"queuing_seat_no":0,"sdk_msg":{}})");
    assert(assigned_state && assigned_state->pod_assigned());
    const std::string assigned=R"({"code":0,"data":{"session_id":"synthetic-session","status":2,"sdk_msg":"{}"}})";
    Reset({{200,assigned}});after_request=[]{cancelled=true;};
    const auto raced=Dispatch("synthetic-login",inputs,&cancelled);
    assert(raced.state && raced.state->session_id=="synthetic-session" && paths.size()==1);
    assert(nlohmann::json::parse(bodies[0])["gameId"]=="opaque-game+/=");
    const auto command_line=nlohmann::json::parse(bodies[0])["cmd_line"].get<std::string>();
    assert(command_line=="-cloud_platform=2 -cloud_display_resolution=1280x720 -cloud_safe_area=0x0x0x0 -cloud_is_web=true");
    assert(BuildCommandLine("1280x720",1,false)=="-cloud_platform=1 -cloud_display_resolution=1280x720 -cloud_safe_area=0x0x0x0 -cloud_is_web=false");
    assert(BuildCommandLine("1920x1080",2,true)=="-cloud_platform=2 -cloud_display_resolution=1920x1080 -cloud_safe_area=0x0x0x0 -cloud_is_web=true");
    Reset({});cancelled=true;
    assert(!Dispatch("synthetic-login",inputs,&cancelled).state && paths.empty());

    Reset({{200,R"({"isSuccess":false,"error":{"code":12345,"msg":"synthetic-error"}})"}});
    const auto failed=Poll("synthetic-login","synthetic-session",&cancelled);
    assert(!failed.state && failed.error.http==200 && failed.error.code==12345 && paths.size()==1);
    Reset({{503,"{}"}});after_request=[]{cancelled=true;};
    assert(!Poll("synthetic-login","synthetic-session",&cancelled).state && paths.size()==1);
    Reset({{200,""}});transport_error_at=1;
    const auto stalled=Poll("synthetic-login","synthetic-session",&cancelled);
    assert(!stalled.state && stalled.error.code==-1028 && stalled.error.http==0 && paths.size()==1);

    const std::string config=R"({"lines":[{"nodeId":"synthetic-node","nodeName":"test"}]})";
    Reset({{404,""},{200,config}});
    const auto fallback=GetNodeList("","synthetic-tenant",&cancelled);
    assert(fallback.deploy_ok && fallback.nodes.size()==1 && paths.size()==2);
    assert(paths[0].starts_with("https://paas-sdk-config.vlinkcloud.cn/ping/") &&
           paths[1].starts_with("https://paas-sdk-config-ks.vlinkcloud.cn/ping/"));
    Reset({{200,config},{200,R"({"code":0,"data":{"node_list":[{"node_id":"synthetic-node","node_name":"test","delay":42}]}})"}});
    const auto enriched=GetNodeList("synthetic-login","synthetic-tenant",&cancelled);
    assert(enriched.nodes.size()==1 && enriched.nodes[0].delay==42 && paths.size()==2);
    Reset({});cancelled=true;
    assert(!GetNodeList("synthetic-login","synthetic-tenant",&cancelled).deploy_ok && paths.empty());
    Reset({{200,R"({"isSuccess":true,"data":{}})"}});
    expected_cancel=nullptr;expected_timeout=5000;cancelled=true;
    assert(Cancel("synthetic-login","synthetic-session").code==0 && paths.size()==1);
    // Exchange receives the app-granted token, separate from the passport.
    Reset({{200,R"({"isSuccess":false,"error":{"code":12345,"msg":"synthetic-error"}})"}});
    const auto web=Exchange("synthetic-cloud-grant","synthetic-game-ticket",&cancelled);
    assert(web.error.code==12345 && web.error.http==200 && paths.size()==1);
    assert(paths[0].ends_with("/api/v1/user/exchange"));
    const auto exchange=nlohmann::json::parse(bodies[0]);
    assert(exchange["hg_token"]=="synthetic-cloud-grant" && exchange["u8_token"]=="synthetic-game-ticket");
    Reset({});
    assert(Exchange("synthetic-cloud-grant","",&cancelled).error.code==-9 && paths.empty());

    // H5 gameManager publishes a matching CLIENT code only after U8 + cloud
    // exchange succeed. Android AuthorizeGame above deliberately does neither.
    const auto web_replies=[] {
        return std::vector<opennow::HttpResponse>{
            {200,R"({"status":0,"data":{"token":"synthetic-web-token","code":"synthetic-web-code"}})"},
            {200,R"({"status":0,"data":{"token":"synthetic-web-u8"}})"},
            {200,R"({"status":0,"data":{"authorized":true,"isLatestUserAgreement":true}})"},
            {200,R"({"code":0,"data":{"login_session_id":"synthetic-refreshed-session","tenant_key":"synthetic-tenant","host_url":"https://example.invalid","game_id":"synthetic-game"}})"}};
    };
    Reset(web_replies());
    auto web_auth=cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","synthetic-proof",&cancelled);
    assert(web_auth.status==0 && web_auth.http==200 && web_auth.code=="synthetic-web-code");
    assert(paths.size()==4 && paths[1].ends_with("/u8/user/auth/v2/token_by_channel_token") &&
           paths[2].find("/agreement_version?token=")!=std::string::npos &&
           paths[3].ends_with("/api/v1/user/exchange"));
    assert(nlohmann::json::parse(bodies[1])["channelToken"]["code"]==web_auth.code);
    assert(nlohmann::json::parse(bodies[1])["platform"]==7);
    const auto web_exchange=nlohmann::json::parse(bodies[3]);
    assert(web_exchange["hg_token"]=="synthetic-web-token" && web_exchange["u8_token"]=="synthetic-web-u8");
    Reset({});
    assert(cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","",&cancelled).status==-22 && paths.empty());
    Reset({{200,R"({"status":109,"data":{}})"}});
    assert(cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","synthetic-proof",&cancelled).status==109 && paths.size()==1);
    auto web_failed=web_replies();web_failed[1]={200,R"({"status":11204,"data":{}})"};Reset(web_failed);
    web_auth=cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","synthetic-proof",&cancelled);
    assert(web_auth.status==11204 && web_auth.code.empty() && paths.size()==2);
    web_failed=web_replies();web_failed[3]={200,R"({"code":12345,"data":{}})"};Reset(web_failed);
    web_auth=cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","synthetic-proof",&cancelled);
    assert(web_auth.status==12345 && web_auth.code.empty() && paths.size()==4);
    web_failed=web_replies();web_failed[2]={200,R"({"status":0,"data":{"authorized":true,"isLatestUserAgreement":false}})"};Reset(web_failed);
    web_auth=cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","synthetic-proof",&cancelled);
    assert(web_auth.status==-23 && web_auth.code.empty() && paths.size()==3); // Never POST agreement acceptance.
    web_failed=web_replies();web_failed[2]={200,R"({"status":0,"data":{}})"};Reset(web_failed);
    web_auth=cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","synthetic-proof",&cancelled);
    assert(web_auth.status==-12 && web_auth.code.empty() && paths.size()==3);
    Reset(web_replies());after_request=[]{cancelled=true;};
    web_auth=cloudgenshin::endfield::login::AuthorizeWebGame("synthetic-passport","synthetic-proof",&cancelled);
    assert(web_auth.status==-2 && web_auth.code.empty() && paths.size()==1);
}
