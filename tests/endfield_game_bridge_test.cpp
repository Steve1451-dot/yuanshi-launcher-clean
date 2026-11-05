#include "endfield/game_bridge.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <cassert>
#include <iostream>

using namespace cloudgenshin::endfield;
using Json=nlohmann::json;

static SimpleCmd Business(const std::string& key,const std::string& body,const std::string& id="1") {
    SimpleCmd inner;inner.cmd=key;inner.arg2="wlweb";
    inner.data=std::vector<std::uint8_t>(body.begin(),body.end());
    SimpleCmd outer;outer.cmd="BusinessData";outer.arg=id;outer.data=inner.Serialize();
    return outer;
}
int main() {
    // An old unconfirmed disconnect must never replay after the newer connect
    // was acknowledged. This used to leave the game seeing a stale pad state.
    for(const auto profile:{GameBridge::Profile::AndroidSdk,GameBridge::Profile::WebRtc}) {
        GameBridge snapshot({},"synthetic-device",profile);
        std::vector<std::vector<std::uint8_t>> writes;
        const auto send_snapshot=[&](const auto& bytes){writes.push_back(bytes);return true;};
        snapshot.pipeline_connected(0);snapshot.poll(0,send_snapshot);
        SimpleCmd ack;ack.cmd="BusinessDataConfirm";ack.arg=SimpleCmd::Parse(writes.back())->arg;
        snapshot.receive(ack,1);
        snapshot.gamepad_connected(true,10);snapshot.poll(10,send_snapshot);
        snapshot.gamepad_connected(false,20);snapshot.poll(20,send_snapshot);
        const auto old_disconnect=SimpleCmd::Parse(writes.back());assert(old_disconnect);
        snapshot.gamepad_connected(true,30);snapshot.poll(30,send_snapshot);
        const auto newest=SimpleCmd::Parse(writes.back());assert(newest && newest->arg1=="GamepadChange");
        ack.arg=old_disconnect->arg;snapshot.receive(ack,31);
        assert(snapshot.pending()==1); // A stale ACK cannot confirm the new snapshot.
        ack.arg=newest->arg;snapshot.receive(ack,32);
        writes.clear();snapshot.poll(1030,send_snapshot);
        assert(writes.empty()); // No old connect/disconnect can be replayed now.

        GameBridge full({},"synthetic-device",profile);
        full.pipeline_connected(0);full.gamepad_connected(true,1);
        for(int id=0;id<14;++id)
            full.receive(Business("GetSafeArea",Json{{"eventId",id}}.dump(),"area-"+std::to_string(id)),2);
        assert(full.pending()==16);
        full.gamepad_connected(false,3); // Must replace old snapshot even with a full queue.
        writes.clear();full.poll(3,send_snapshot);
        unsigned changes=0,queries=0;
        for(const auto& bytes:writes) {
            const auto item=SimpleCmd::Parse(bytes);assert(item);
            if(item->arg1=="GamepadChange") {
                ++changes;const auto data=Json::parse(item->data->begin(),item->data->end());
                assert(data["data"][0]["connectStatus"]==0);
            }
            if(item->arg1=="OnGetSafeArea") ++queries;
        }
        assert(changes==1 && queries==14 && full.pending()==16);
    }
    {
        GameBridge sensors("","",GameBridge::Profile::WebRtc);
        cloudgenshin::input::MotionSample sample;
        sample.valid=true;sample.gravity={0.6f,0.8f,0};
        sample.orientation=cloudgenshin::input::MotionQuaternion{0.5f,0.5f,0.5f,0.5f};
        assert(sensors.gravity_frame(sample).empty());
        assert(sensors.receive(Business("SensorSwitch",R"({"eventId":1,"data":{"gravity":true}})"),0)==GameBridge::Request::SensorSwitch);
        assert(sensors.gravity_requested() && sensors.pending()==0);
        const auto frame=SimpleCmd::Parse(sensors.gravity_frame(sample));
        assert(frame && frame->cmd=="HighFqDataToGame" && frame->arg1=="SensorEvent");
        assert(!frame->arg && !frame->arg2 && frame->data);
        const auto json=Json::parse(frame->data->begin(),frame->data->end());
        assert(json["eventId"]==0 && json["key"]=="SensorEvent" && json["isGlobalEvent"]==true && json["code"]==0);
        assert(std::abs(json["data"]["gravity"]["x"].get<float>()+0.8f)<1e-6f);
        assert(std::abs(json["data"]["gravity"]["y"].get<float>()-0.6f)<1e-6f);
        assert(json["data"]["gravity"]["z"]==0);
        assert(json["data"].size()==1 && json["data"].contains("gravity")); // No invented Endfield quaternion payload.
        assert(sensors.receive(Business("SensorSwitch",R"({"eventId":2,"data":{"gravity":1}})","2"),1)==GameBridge::Request::Invalid);
        assert(sensors.gravity_requested());
        assert(sensors.receive(Business("SensorSwitch",R"({"eventId":3,"data":"{\"gravity\":false}"})","3"),2)==GameBridge::Request::SensorSwitch);
        assert(!sensors.gravity_requested() && sensors.gravity_frame(sample).empty() && sensors.pending()==0);
        assert(sensors.receive(Business("SensorSwitch",R"({"eventId":4,"data":{"gyroscope":true}})","4"),3)==GameBridge::Request::SensorSwitch);
        assert(!sensors.gravity_requested()); // Generic SDK gyro is not Endfield gravity.
    }
    GameBridge bridge("synthetic-oauth-code","synthetic-device-id");
    assert(bridge.pipeline_connected(0));
    assert(bridge.pipeline_connected(1));
    assert(bridge.pending()==1);
    std::vector<std::vector<std::uint8_t>> sent;
    const auto send=[&](const auto& bytes){sent.push_back(bytes);return true;};
    bridge.poll(0,send);assert(sent.size()==1);
    const auto pipeline=SimpleCmd::Parse(sent.back());
    assert(pipeline && pipeline->cmd=="DataToGameWithKey");
    assert(pipeline->arg=="0" && pipeline->arg1=="PipelineConnected");
    const auto payload=Json::parse(pipeline->data->begin(),pipeline->data->end());
    assert(payload["isGlobalEvent"]==true && payload["eventId"]==0);
    assert(payload["data"]["platform"]==1 && payload["data"]["isWeb"]==false);
    assert(payload["data"]["displayResolution"]["width"]==1280);
    assert(payload["data"]["loginInfo"]==Json::object()); // APK defers game login.
    assert(payload.dump().find("synthetic-oauth-code")==std::string::npos);
    assert(payload["data"]["deviceProperties"].is_string());
    const auto properties=Json::parse(payload["data"]["deviceProperties"].get<std::string>());
    assert(properties["#device_id"]=="synthetic-device-id");
    assert(properties["#sync_device_id"]==properties["#device_id"]);
    assert(properties["#channel1"]=="1" && properties["#channel2"]=="1");
    assert(properties["#os"]=="Android" && properties["#model"]=="Switch");
    assert(properties["#width"]==1280 && properties["#height"]==720);
    assert(properties["#bundle_id"]=="com.hypergryph.cloud.endfield" &&
           properties["#app_vn"]=="1.6.0" && properties["#app_vc"]=="100006000");
    assert(!properties.contains("#sdk_type") && !properties.contains("#browser") && !properties.contains("#shumei_id"));
    assert(properties.dump().find("synthetic-oauth-code")==std::string::npos);
    bridge.poll(999,send);assert(sent.size()==1);
    bridge.poll(1000,send);assert(sent.size()==2 && sent.front()==sent.back());
    SimpleCmd confirm;confirm.cmd="BusinessDataConfirm";confirm.arg="0";
    bridge.receive(confirm,1100);assert(bridge.pending()==0 && bridge.confirmed()==1);
    bridge.poll(2000,send);assert(sent.size()==2);

    const auto login=Business("RequestLogin",R"({"eventId":42,"data":{}})");
    assert(bridge.receive(login,2000)==GameBridge::Request::Login);
    assert(bridge.receive(login,2001)==GameBridge::Request::Unknown);
    assert(bridge.needs_login());
    assert(bridge.complete_login("synthetic-refreshed-oauth-code",2001));
    assert(bridge.pending()==1);
    bridge.poll(2001,send);
    const auto reply=SimpleCmd::Parse(sent.back());assert(reply && reply->arg1=="OnLoginResult");
    const auto login_payload=Json::parse(reply->data->begin(),reply->data->end());
    assert(login_payload["eventId"]==42 && login_payload["isGlobalEvent"]==false);
    assert(login_payload["data"]["msg"]["isSuc"]==true);
    assert(login_payload["data"]["msg"]["code"]=="synthetic-refreshed-oauth-code");
    assert(bridge.receive(Business("GetSafeArea",R"({"eventId":43})","2"),2002)==GameBridge::Request::SafeArea);
    assert(bridge.receive(Business("RequestLaunchInfo",R"({"eventId":44})","3"),2002)==GameBridge::Request::LaunchInfo);
    assert(bridge.receive(Business("GetGamepad",R"({"eventId":45})","4"),2002)==GameBridge::Request::Gamepad);
    bridge.poll(2002,send);
    const auto queried_pad=SimpleCmd::Parse(sent.back());
    assert(queried_pad && queried_pad->arg1=="OnGetGamepad");
    const auto launch_reply=SimpleCmd::Parse(sent[sent.size()-2]);
    assert(launch_reply && launch_reply->arg1=="onRequestLaunchInfo");
    assert(Json::parse(launch_reply->data->begin(),launch_reply->data->end())["key"]=="onRequestLaunchInfo");
    const auto before=bridge.pending();
    assert(bridge.receive(Business("RequestPay",R"({"eventId":46})","5"),2002)==GameBridge::Request::Unsupported);
    assert(bridge.unsupported_command_hash()>0);
    assert(bridge.receive(Business("RequestPay",R"({"eventId":46})","5"),2003)==GameBridge::Request::Unknown);
    assert(bridge.pending()==before+1); // APK negative response; no purchase/UI action.
    bridge.poll(2002,send);
    const auto unsupported=SimpleCmd::Parse(sent.back());assert(unsupported && unsupported->arg1=="RequestPay");
    const auto unsupported_reply=Json::parse(unsupported->data->begin(),unsupported->data->end());
    assert(unsupported_reply["code"]==-5 && unsupported_reply["eventId"]==46 && unsupported_reply["data"]==Json::object());
    auto unkeyed=login;unkeyed.data=std::vector<std::uint8_t>{1,2,3};
    assert(bridge.receive(unkeyed,2003)==GameBridge::Request::Unknown);
    assert(bridge.receive(Business("RequestLogin","not json","6"),2003)==GameBridge::Request::Invalid);
    assert(bridge.receive(Business("RequestLogin",std::string(65537,'x'),"7"),2003)==GameBridge::Request::Invalid);
    assert(bridge.receive(Business("RequestLogin",std::string(32,'[')+std::string(32,']'),"8"),2003)==GameBridge::Request::Invalid);
    bridge.poll(18000,send);assert(bridge.pending()==0 && bridge.expired()==5);
    assert(bridge.receive(Business("SetU8Token",R"({"eventId":50,"data":{"token":"synthetic-u8-token"}})","set"),18000)==GameBridge::Request::SetToken);
    assert(bridge.receive(Business("RequestLaunchInfo",R"({"eventId":51})","launch-again"),18000)==GameBridge::Request::LaunchInfo);
    bridge.poll(18000,send);
    const auto refreshed_launch=SimpleCmd::Parse(sent.back());assert(refreshed_launch && refreshed_launch->data);
    const auto refreshed_info=Json::parse(refreshed_launch->data->begin(),refreshed_launch->data->end());
    assert(refreshed_info["data"]["loginInfo"]["msg"]["code"]=="synthetic-refreshed-oauth-code");
    assert(Json::parse(refreshed_info["data"]["deviceProperties"].get<std::string>())==properties);
    assert(bridge.receive(Business("GetSafeArea",R"({"eventId":"49"})","9"),18001)==GameBridge::Request::SafeArea);
    assert(bridge.receive(Business("GetSafeArea",R"({"eventId":"49invalid"})","10"),18001)==GameBridge::Request::Invalid);

    GameBridge missing;
    assert(missing.pipeline_connected(0) && missing.pending()==1);
    missing.poll(0,send);
    const auto missing_start=SimpleCmd::Parse(sent.back());assert(missing_start && missing_start->data);
    const auto missing_login=Json::parse(missing_start->data->begin(),missing_start->data->end())["data"]["loginInfo"];
    assert(missing_login==Json::object());
    missing.receive(Business("RequestLogin",R"({"eventId":1})"),1);
    assert(missing.complete_login("",1));
    missing.poll(1,send);
    const auto failed_login=SimpleCmd::Parse(sent.back());assert(failed_login && failed_login->arg1=="OnLoginResult");
    const auto failed_login_data=Json::parse(failed_login->data->begin(),failed_login->data->end())["data"];
    assert(failed_login_data["code"]==2 && failed_login_data["msg"].is_number_integer());
    GameBridge lifecycle;
    const auto account_login=Business("SubmitGameData",R"({"eventId":1,"data":{"dataType":8,"uid":"synthetic-private-id"}})","account");
    assert(lifecycle.receive(account_login,0)==GameBridge::Request::AccountLogin);
    assert(lifecycle.game_data_type()==8);
    assert(lifecycle.receive(account_login,1)==GameBridge::Request::Unknown);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":2,"data":{"dataType":3,"roleID":"synthetic-role"}})","character"),2)==GameBridge::Request::CharacterLogin);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":3,"data":{"dataType":6}})","character-left"),3)==GameBridge::Request::CharacterLogout);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":4,"data":{"dataType":9}})","account-left"),4)==GameBridge::Request::AccountLogout);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":5,"data":{"dataType":7}})","process"),5)==GameBridge::Request::GameData);
    assert(lifecycle.game_data_type()==7);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":8,"data":{"dataType":9999999999,"uid":"synthetic-private-id"}})","unknown-type"),5)==GameBridge::Request::GameData);
    assert(lifecycle.game_data_type()==-1);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":9,"data":{"dataType":-2}})","negative-type"),5)==GameBridge::Request::GameData);
    assert(lifecycle.game_data_type()==-1);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":6,"data":{"dataType":"3"}})","wrong-type"),6)==GameBridge::Request::Invalid);
    assert(lifecycle.receive(Business("SubmitGameData",R"({"eventId":7,"data":{}})","missing-type"),7)==GameBridge::Request::Invalid);
    assert(lifecycle.pending()==0); // Official notifications have no reply, including private IDs.
    assert(lifecycle.receive(Business("SendEventLog",R"({"eventId":10,"data":{"method":"userLoginEventV2","params":{"userId":"synthetic-private-id"}}})","telemetry"),8)==GameBridge::Request::Telemetry);
    assert(lifecycle.pending()==0); // No upload, reflected private fields or fake negative SDK reply.
    GameBridge browser;
    assert(browser.receive(Business("CloudBrowserEvent",R"({"eventId":101,"data":{"method":"Constructor"}})","browser-init"),0)==GameBridge::Request::BrowserControl);
    browser.poll(0,send);
    const auto constructed=SimpleCmd::Parse(sent.back());assert(constructed && constructed->arg1=="CloudBrowserEvent");
    assert(Json::parse(constructed->data->begin(),constructed->data->end())["code"]==-5);
    assert(browser.receive(Business("CloudBrowserEvent",R"({"eventId":102,"data":{"method":"Open","params":{"url":"https://example.invalid/private?synthetic-secret"}}})","browser-open"),1)==GameBridge::Request::BrowserOpen);
    browser.poll(1,send);
    const auto unavailable=SimpleCmd::Parse(sent.back());assert(unavailable && unavailable->arg1=="OnCloudBrowserEvent");
    const auto unavailable_json=Json::parse(unavailable->data->begin(),unavailable->data->end());
    assert(unavailable_json["data"]["code"]!=0 && unavailable_json["eventId"]==102);
    assert(unavailable_json.dump().find("synthetic-secret")==std::string::npos);
    const auto before_language=browser.pending();
    assert(browser.receive(Business("SetLanguage",R"({"eventId":103,"data":{"language":"zh-cn"}})","language"),2)==GameBridge::Request::Language);
    assert(browser.pending()==before_language);
    assert(browser.receive(Business("CloudBrowserEvent",R"({"eventId":104,"data":{"method":"unknown-startup-method"}})","browser-unknown"),3)==GameBridge::Request::BrowserControl);
    browser.poll(3,send);
    const auto no_action=SimpleCmd::Parse(sent.back());assert(no_action && no_action->arg1=="CloudBrowserEvent");
    assert(Json::parse(no_action->data->begin(),no_action->data->end())["code"]==-5);
    const auto before_close=browser.pending();
    assert(browser.receive(Business("CloudBrowserEvent",R"({"eventId":105,"data":"{\"method\":\"Close\"}"})","browser-close"),4)==GameBridge::Request::BrowserControl);
    assert(browser.pending()==before_close);
    assert(browser.receive(Business("SubmitGameData",R"({"eventId":106,"data":"{\"dataType\":8}"})","string-data"),5)==GameBridge::Request::AccountLogin);
    GameBridge bounded("synthetic-ticket");
    for(int i=0;i<20;++i)
        bounded.receive(Business("GetSafeArea",Json{{"eventId",i}}.dump(),std::to_string(i)),0);
    assert(bounded.pending()==16);
    unsigned failed_sends=0;
    bounded.poll(0,[&](const auto&){++failed_sends;return false;});
    bounded.poll(99,[&](const auto&){++failed_sends;return false;});
    assert(failed_sends==16);
    bounded.poll(100,[&](const auto&){++failed_sends;return true;});
    assert(failed_sends==32);
    GameBridge controller("synthetic-ticket");
    controller.gamepad_connected(true,0);
    controller.pipeline_connected(0);
    controller.receive(Business("GetGamepad",R"({"eventId":1})"),1);
    controller.poll(1,send);
    const auto pads=SimpleCmd::Parse(sent.back());assert(pads && pads->arg1=="OnGetGamepad");
    const auto slots=Json::parse(pads->data->begin(),pads->data->end())["data"];
    assert(slots.size()==4 && slots[0]["gamepadType"]==11 && slots[0]["connectStatus"]==1);
    assert(slots[1]["connectStatus"]==0);
    controller.gamepad_connected(false,2);
    controller.poll(2,send);
    const auto unplug=SimpleCmd::Parse(sent.back());assert(unplug && unplug->arg1=="GamepadChange");
    const auto changed=Json::parse(unplug->data->begin(),unplug->data->end());
    assert(changed["isGlobalEvent"]==true && changed["data"][0]["connectStatus"]==0);
    assert(controller.receive(Business("GamepadChange",R"({"eventId":2})","pad-query"),3)==GameBridge::Request::Gamepad);
    controller.poll(3,send);
    const auto queried_change=SimpleCmd::Parse(sent.back());assert(queried_change && queried_change->arg1=="OnGetGamepad");
    const auto queried_change_json=Json::parse(queried_change->data->begin(),queried_change->data->end());
    assert(queried_change_json["eventId"]==2 && queried_change_json["isGlobalEvent"]==false);
    GameBridge web("synthetic-fresh-unredeemed-code","synthetic-device",GameBridge::Profile::WebRtc);
    web.pipeline_connected(0);web.poll(0,send);
    const auto web_start=SimpleCmd::Parse(sent.back());assert(web_start && web_start->arg1=="PipelineConnected");
    const auto web_launch=Json::parse(web_start->data->begin(),web_start->data->end())["data"];
    assert(web_launch["platform"]==7 && web_launch["isWeb"]==true);
    assert(web_launch["loginInfo"]["msg"]["code"]=="synthetic-fresh-unredeemed-code");
    assert(Json::parse(web_launch["deviceProperties"].get<std::string>())["#channel2"]=="87101098");
    web.receive(Business("RequestLaunchInfo",R"({"eventId":1})"),1);web.poll(1,send);
    assert(SimpleCmd::Parse(sent.back())->arg1=="OnRequestLaunchInfo");
    web.receive(Business("GetGamepad",R"({"eventId":2})","web-pad"),2);web.poll(2,send);
    assert(SimpleCmd::Parse(sent.back())->arg1=="OnGamepadChange");
    web.receive(Business("CloudBrowserEvent",R"({"eventId":3,"data":{"method":"Constructor"}})","web-browser"),3);
    web.poll(3,send);assert(SimpleCmd::Parse(sent.back())->arg1=="OnBrowserOpenResult");
    GameBridge failed_web({},"synthetic-device",GameBridge::Profile::WebRtc);
    failed_web.receive(Business("RequestLogin",R"({"eventId":10})"),1);
    assert(failed_web.complete_login({},1));failed_web.poll(1,send);
    const auto failed_web_message=SimpleCmd::Parse(sent.back());
    assert(failed_web_message && failed_web_message->arg1=="OnLoginResult");
    const auto failed_web_data=Json::parse(failed_web_message->data->begin(),failed_web_message->data->end())["data"];
    assert(failed_web_data["code"]==2 && failed_web_data["msg"]["code"]=="" &&
           failed_web_data["msg"]["isSuc"]==false && failed_web_data["msg"]["type"]==1);
    // A response deadline is not a successful SDK completion. The same
    // incoming request must remain recoverable after all its writes expire.
    for(const auto profile:{GameBridge::Profile::AndroidSdk,GameBridge::Profile::WebRtc}) {
        GameBridge retry({},"synthetic-device",profile);
        const auto query=Business("GetSafeArea",R"({"eventId":201})","retry-query");
        assert(retry.receive(query,0)==GameBridge::Request::SafeArea);
        retry.poll(0,send);
        const auto first_response=SimpleCmd::Parse(sent.back());assert(first_response);
        assert(retry.receive(query,14999)==GameBridge::Request::Unknown);
        retry.poll(15000,send);
        assert(retry.pending()==0 && retry.expired()==1);
        assert(retry.receive(query,15000)==GameBridge::Request::SafeArea);
        retry.poll(15000,send);
        const auto renewed_response=SimpleCmd::Parse(sent.back());assert(renewed_response);
        assert(first_response->arg!=renewed_response->arg);
        SimpleCmd late_ack;late_ack.cmd="BusinessDataConfirm";late_ack.arg=first_response->arg;
        retry.receive(late_ack,15001);assert(retry.pending()==1);
        late_ack.arg=renewed_response->arg;retry.receive(late_ack,15002);
        assert(retry.pending()==0 && retry.confirmed()==1);
        assert(retry.receive(query,15003)==GameBridge::Request::Unknown);

        GameBridge retry_login({},"synthetic-device",profile);
        const auto request=Business("RequestLogin",R"({"eventId":202})","retry-login");
        assert(retry_login.receive(request,0)==GameBridge::Request::Login);
        assert(retry_login.complete_login("synthetic-expired-reply-code",1));
        retry_login.poll(1,send);retry_login.poll(15001,send);
        assert(retry_login.pending()==0 && retry_login.expired()==1);
        assert(retry_login.receive(request,15002)==GameBridge::Request::Login);
        assert(retry_login.needs_login());
        assert(retry_login.complete_login("synthetic-new-retry-code",15003));
        retry_login.poll(15003,send);
        const auto renewed_login=SimpleCmd::Parse(sent.back());assert(renewed_login);
        const auto renewed_body=Json::parse(renewed_login->data->begin(),renewed_login->data->end());
        assert(renewed_body["eventId"]==202);
        assert(renewed_body["data"]["msg"]["code"]=="synthetic-new-retry-code");
        assert(renewed_body.dump().find("synthetic-expired-reply-code")==std::string::npos);

        GameBridge busy({},"synthetic-device",profile);
        assert(busy.receive(query,0)==GameBridge::Request::SafeArea);
        assert(busy.receive(request,0)==GameBridge::Request::Login);
        for(int i=0;i<40;++i)
            busy.receive(Business("SendEventLog",R"({"eventId":0,"data":{}})",std::to_string(i)),1);
        assert(busy.receive(query,2)==GameBridge::Request::Unknown);
        assert(busy.receive(request,2)==GameBridge::Request::Unknown);
        assert(busy.pending()==1 && busy.needs_login());

        // Network delivery precedes the worker's poll. A retransmission at
        // the deadline must recover even without an intervening timer poll.
        GameBridge boundary({},"synthetic-device",profile);
        assert(boundary.receive(query,0)==GameBridge::Request::SafeArea);
        boundary.poll(0,[](const auto&){return false;});
        assert(boundary.receive(query,15000)==GameBridge::Request::SafeArea);
        assert(boundary.pending()==1 && boundary.expired()==1);
        boundary.poll(15000,send);
        const auto boundary_reply=SimpleCmd::Parse(sent.back());assert(boundary_reply);
        assert(Json::parse(boundary_reply->data->begin(),boundary_reply->data->end())["eventId"]==201);
    }
    std::cout<<"PASS Endfield game bridge: login, wire framing, confirmations, bounded retries\n";
}
