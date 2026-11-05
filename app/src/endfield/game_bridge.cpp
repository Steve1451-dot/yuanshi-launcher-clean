#include "endfield/game_bridge.hpp"
#include "endfield/endfield_profile.hpp"
#include "endfield/account_policy.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <utility>

namespace cloudgenshin::endfield {
namespace {
using Json = nlohmann::ordered_json;
constexpr std::size_t kMaxBusinessBytes=65536;
Json ParseBounded(const std::vector<std::uint8_t>& bytes) {
    if(bytes.empty() || bytes.size()>kMaxBusinessBytes) return {};
    // Reject excessive nesting before the DOM parser sees an untrusted body.
    bool string=false,escape=false;
    int depth=0;
    for(const unsigned char c:bytes) {
        if(string) {
            if(escape) escape=false;
            else if(c=='\\') escape=true;
            else if(c=='"') string=false;
        } else if(c=='"') string=true;
        else if(c=='{' || c=='[') {if(++depth>16) return {};}
        else if(c=='}' || c==']') {if(--depth<0) return {};}
    }
    return Json::parse(bytes.begin(),bytes.end(),nullptr,false);
}
Json Resolution() {return {{"width",kDispatchWidth},{"height",kDispatchHeight}};}
Json SafeArea() {return {{"left",0},{"top",0},{"right",0},{"bottom",0}};}
Json Reply(const std::string& key, const Json& data, const Json& event, bool global=false,int code=0) {
    return {{"data",data},{"code",code},{"eventId",event},
            {"isGlobalEvent",global},{"key",key},
            {"message",code==-5 ? "CloudCommand is not support by client" : "success"}};
}
} // namespace

GameBridge::GameBridge(std::string auth_code,std::string client_device_id,Profile profile)
    : auth_code_(std::move(auth_code)),client_device_id_(std::move(client_device_id)),profile_(profile) {}

std::string GameBridge::login_info() const {
    // Official eH / ta reads K.B4 -> q, the OAuth CLIENT grant's data.code.
    // U8 token c is stored separately and is not the game's type-1 code.
    if(auth_code_.empty()) {
        if(profile_==Profile::WebRtc)
            return Json{{"code",2},{"msg",{{"code",""},{"isSuc",false},{"type",1}}}}.dump();
        return Json{{"code",2},{"msg",1001}}.dump();
    }
    return Json{{"code",0},
                {"msg",{{"code",auth_code_},{"isSuc",!auth_code_.empty()},{"type",1}}}}.dump();
}

std::string GameBridge::launch_info() const {
    // APK CloudBridge calls getStaticPresetProperties, NOT getPresetProperties.
    // EventClass.getStaticProperties contains device/app/channel fields and
    // excludes the browser/page/EventLog SDK fields previously sent here.
    // Package/version identify the APK wire compatibility profile (BuildConfig
    // and HeaderInterceptor), not this port's UI release. Device/model fields
    // describe the native host; unknown hardware/ad/risk ids remain absent.
    // "Android" selects the APK bridge compatibility profile, not a claimed
    // phone model, Android installation, or successful device verification.
    Json properties={{"#device_id",client_device_id_},
        {"#sync_device_id",client_device_id_},{"#height",kDispatchHeight},
        {"#width",kDispatchWidth},{"#os","Android"},{"#os_ver",""},
        {"#brand","Nintendo"},{"#model","Switch"},
        {"#bundle_id","com.hypergryph.cloud.endfield"},
        {"#app_vn",std::string(kClientAppVersion)},{"#app_vc","100006000"},
        {"#channel1","1"},{"#channel2","1"}};
    const bool web=profile_==Profile::WebRtc;
    if(web) {
        // The H5 transport's companion gameManager uses a different bridge
        // dialect from APK CloudBridge. Never mix its callback names/profile
        // with the Android SDK just because both use SimpleCmd underneath.
        properties={{"#sdid_s",""},{"#sdid_c",client_device_id_},
            {"#device_id",client_device_id_},{"#sdk_type","Web JS"},
            {"#sdk_vn","4.6.1"},{"#sdk_vc","1"},{"#os","Windows"},
            {"#os_ver","10"},{"#ua",std::string(account::kUserAgent)},
            {"#browser","Chrome"},{"#browser_ver","124.0.0.0"},
            {"#title","OpenNOW / Endfield"},{"#url",""},{"#url_path",""},
            {"#referrer",""},{"#referrer_host",""},{"#tracking_code",""},
            {"#is_first_page_view",true},{"#channel1","1"},{"#channel2","87101098"}};
    }
    return Json{{"platform",web?7:kGameBridgePlatform},{"deviceProperties",properties.dump()},
                {"dpi",1},{"displayResolution",Resolution()},{"safeArea",SafeArea()},
                {"isWeb",web},
                // APK LoginService.cloudLoginInfo starts as an empty object;
                // only cloudLogin's completed callback populates it. A cached
                // OAuth code is not an already-completed game login result.
                {"loginInfo",web || login_completed_ ? Json::parse(login_info()) : Json::object()}}.dump();
}

bool GameBridge::enqueue(const std::string& key,const std::string& json,std::int64_t now,
                         std::string request_identity,bool gamepad_snapshot) {
    const auto obsolete=gamepad_snapshot ? std::count_if(pending_.begin(),pending_.end(),
        [](const auto& item){return item.gamepad_snapshot;}) : 0;
    if(pending_.size()-obsolete>=16 || json.size()>kMaxBusinessBytes) return false;
    SimpleCmd out;
    out.cmd="DataToGameWithKey";
    out.arg=std::to_string(next_index_++);
    out.arg1=key;
    out.data=std::vector<std::uint8_t>(json.begin(),json.end());
    auto bytes=out.Serialize();
    // GamepadChange is a replaceable current-state notification, not a call
    // response. Retrying an older disconnect after a new connect was ACKed
    // would regress the game's state. Keep reply/launch/login entries intact.
    if(gamepad_snapshot) std::erase_if(pending_,[](const auto& item){return item.gamepad_snapshot;});
    pending_.push_back({*out.arg,std::move(bytes),now,now+15000,std::move(request_identity),gamepad_snapshot});
    return true;
}

bool GameBridge::pipeline_connected(std::int64_t now) {
    if(started_) return true;
    started_=enqueue("PipelineConnected",Reply("PipelineConnected",
        Json::parse(launch_info()),0,true).dump(),now);
    return started_;
}

std::string GameBridge::gamepads() const {
    auto pads=Json::array();
    for(int i=0;i<4;++i) {
        const bool connected=i==0 && gamepad_connected_;
        // Official module 60383: Xbox=11, full control=1. Physical Switch
        // controls are mapped to this single virtual Xbox slot by wl_input.
        pads.push_back({{"index",i},{"gamepadType",connected?11:0},
                        {"gamepadControlType",connected?1:0},{"connectStatus",connected?1:0}});
    }
    return pads.dump();
}

void GameBridge::gamepad_connected(bool connected,std::int64_t now) {
    if(connected==gamepad_connected_) return;
    const bool previous=gamepad_connected_;
    gamepad_connected_=connected;
    if(started_ && !enqueue("GamepadChange",Reply("GamepadChange",
        Json::parse(gamepads()),0,true).dump(),now,{},true))
        gamepad_connected_=previous; // Retry a rejected transition next poll.
}

GameBridge::Request GameBridge::receive(const SimpleCmd& message,std::int64_t now) {
    if(message.cmd=="BusinessDataConfirm" && message.arg) {
        const auto found=std::find_if(pending_.begin(),pending_.end(),
            [&](const auto& item){return item.index==*message.arg;});
        if(found!=pending_.end()) {pending_.erase(found);++confirmed_;}
        return Request::Unknown;
    }
    if(message.cmd!="BusinessData" || !message.data) return Request::Unknown;
    if(message.data->size()>kMaxBusinessBytes) return Request::Invalid;
    // RTC receive is drained before poll(). Do not let a request arriving
    // at the deadline match a reply that the next poll is about to discard.
    // ACKs above are still processed first if the reply was confirmed late.
    expire_pending(now);
    // Qm in module 6718 is another SimpleCmd decoder, not a JSON or MsgPack
    // envelope. Field 5 starts with "wl" for the keyed game pipeline.
    const auto inner=SimpleCmd::Parse(*message.data);
    if(!inner || !inner->arg2 || inner->arg2->rfind("wl",0)!=0 || !inner->data)
        return Request::Unknown;
    auto request=ParseBounded(*inner->data);
    if(!request.is_object()) return Request::Invalid;
    // APK CloudCommandModel stores data via JSONObject.optString: pods may
    // supply either a JSON object or that object's JSON string representation.
    if(auto item=request.find("data");item!=request.end() && item->is_string()) {
        const auto text=item->get<std::string>();
        const auto value=ParseBounded(std::vector<std::uint8_t>(text.begin(),text.end()));
        if(value.is_object() || value.is_array()) *item=value;
    }
    Json event=0;
    if(const auto it=request.find("eventId");it!=request.end()) {
        if(it->is_number_integer() || it->is_number_unsigned()) event=*it;
        else if(it->is_string()) {
            const auto& text=it->get_ref<const std::string&>();
            std::int64_t number=0;
            const auto result=std::from_chars(text.data(),text.data()+text.size(),number);
            if(result.ec!=std::errc() || result.ptr!=text.data()+text.size()) return Request::Invalid;
            event=number;
        } else return Request::Invalid;
    }
    // Pod retransmits until BusinessDataConfirm. Do not produce another login
    // response with a new outgoing index when its reply is already in flight.
    const std::string identity=message.arg.value_or("")+":"+inner->cmd+":"+event.dump();
    // Pending replies/auth refresh outlive the small recent-request cache.
    // A burst of telemetry must not evict their in-flight duplicate guard.
    if((login_event_ && login_identity_==identity) ||
       std::any_of(pending_.begin(),pending_.end(),[&](const auto& pending) {
           return pending.request_identity==identity;
       }) || std::find(handled_.begin(),handled_.end(),identity)!=handled_.end())
        return Request::Unknown;
    std::uint32_t command_hash=2166136261u;
    for(const unsigned char c:inner->cmd) {command_hash^=c;command_hash*=16777619u;}
    unsupported_command_hash_=command_hash & 0x7fffffffu;
    std::string key;
    Json data;
    int result_code=0;
    Request kind=Request::Unknown;
    if(inner->cmd=="RequestLaunchInfo") {
        // Android 1.6.0 CloudUploadCommand / CloudBridge uses a lowercase
        // initial 'o'. The transport confirms even unrecognised game keys,
        // so a BusinessDataConfirm alone cannot validate this callback name.
        kind=Request::LaunchInfo;
        key=profile_==Profile::WebRtc ? "OnRequestLaunchInfo" : "onRequestLaunchInfo";
        data=Json::parse(launch_info());
    } else if(inner->cmd=="RequestLogin") {
        // Official ta calls checkAndUpdateToken before replying: an old code
        // may already be consumed. The owner refreshes it off the RTC worker.
        if(login_event_) return Request::Invalid;
        login_event_=event.dump();login_identity_=identity;kind=Request::Login;
    } else if(inner->cmd=="GetSafeArea") {
        kind=Request::SafeArea;key="OnGetSafeArea";
        data={{"displayResolution",Resolution()},{"safeArea",SafeArea()}};
    } else if(inner->cmd=="GetGamepad" || inner->cmd=="GamepadChange") {
        // APK GamepadManager.replyOnGetGamepad handles both request keys.
        // GamepadChange is reserved for unsolicited global notifications.
        kind=Request::Gamepad;
        key=profile_==Profile::WebRtc ? "OnGamepadChange" : "OnGetGamepad";
        data=Json::parse(gamepads());
    } else if(inner->cmd=="SensorSwitch") {
        const auto item=request.find("data");
        if(item==request.end() || !item->is_object()) return Request::Invalid;
        const auto gravity=item->find("gravity");
        if(gravity!=item->end()) {
            if(!gravity->is_boolean()) return Request::Invalid;
            gravity_requested_=gravity->get<bool>();
        }
        // APK PlayGameViewModel.onSensorSwitch has no game callback reply.
        // Transport BusinessDataConfirm is still handled by the session.
        kind=Request::SensorSwitch;
    } else if(inner->cmd=="SubmitGameData") {
        // Official module 6718 distinguishes account login (8), character
        // login (3), character logout (6) and account logout (9). A pod's
        // earlier START_GAME only means that its game process was launched.
        // These notifications have no reply. IDs and other telemetry remain
        // inside the received message and are never copied to diagnostics.
        const auto item=request.find("data");
        if(item==request.end() || !item->is_object()) return Request::Invalid;
        const auto type=item->find("dataType");
        if(type==item->end() || !type->is_number_integer()) return Request::Invalid;
        // ExtraGameData declares 1..11. Unknown numbers remain an explicit
        // local sentinel; do not reflect an arbitrary remote integer as a
        // diagnostic or copy any account/role/custom-event fields.
        game_data_type_=*type>=1 && *type<=11 ? type->get<int>() : -1;
        kind=*type==8 ? Request::AccountLogin : *type==3 ? Request::CharacterLogin :
             *type==6 ? Request::CharacterLogout : *type==9 ? Request::AccountLogout : Request::GameData;
    } else if(inner->cmd=="CloudBrowserEvent") {
        // APK WebViewService: Open replies on OnCloudBrowserEvent, Close has
        // no separate response, and unknown methods return -5 on the original
        // request key. Never claim that an unsupported page/action succeeded.
        const auto item=request.find("data");
        if(item==request.end() || !item->is_object()) return Request::Invalid;
        const auto method=item->find("method");
        if(method==item->end() || !method->is_string()) return Request::Invalid;
        if(*method=="Open" || *method=="OpenBrowser") {
            kind=Request::BrowserOpen;
            key=profile_==Profile::WebRtc ? "OnBrowserOpenResult" : "OnCloudBrowserEvent";
            // An external web view is not implemented. Report that honestly;
            // never mark a protocol/privacy/payment page accepted or opened.
            data={{"code",1},{"msg","External browser unavailable"}};
        } else if(profile_==Profile::WebRtc) {
            kind=Request::BrowserControl;key="OnBrowserOpenResult";data={{"code",0},{"msg",""}};
        } else if(*method=="Close") {
            kind=Request::BrowserControl;
        } else {
            kind=Request::BrowserControl;key=inner->cmd;data=Json::object();result_code=-5;
        }
    } else if(inner->cmd=="SendEventLog") {
        // Web module 6718 tP delegates local analytics and returns null. This
        // port does not upload analytics; do not reply with a fabricated
        // unsupported SDK result or copy any personal telemetry into logs.
        kind=Request::Telemetry;
    } else if(inner->cmd=="SetLanguage") {
        // Official td updates only web content localization and has no reply.
        // Native UI localization is owned by the user's launcher setting.
        kind=Request::Language;
    } else if(inner->cmd=="SetU8Token") {
        const auto item=request.find("data");
        if(item==request.end() || !item->is_object()) return Request::Invalid;
        const auto value=item->find("token");
        if(value==item->end() || !value->is_string() || value->get_ref<const std::string&>().size()>8192)
            return Request::Invalid;
        // This is the game's U8 cache update, not a new OAuth login code.
        kind=Request::SetToken;
    } else {
        // CloudBridge's unsupported-command response preserves the event/key
        // and returns COMMAND_NOT_SUPPORTED. No account, payment, page or
        // consent action is performed, and no request payload is reflected.
        kind=Request::Unsupported;key=inner->cmd;data=Json::object();result_code=-5;
    }
    if(!key.empty() && !enqueue(key,Reply(key,data,event,false,result_code).dump(),now,identity))
        return Request::Invalid;
    handled_.push_back(identity);
    if(handled_.size()>32) handled_.pop_front();
    return kind;
}

std::vector<std::uint8_t> GameBridge::gravity_frame(const cloudgenshin::input::MotionSample& sample) const {
    if(!gravity_requested_ || !sample.valid || !cloudgenshin::input::FiniteMotion(sample.gravity) ||
       std::abs(cloudgenshin::input::MotionLength(sample.gravity)-1.0f)>0.02f) return {};
    // APK landscape-left adjustment (-y,x,z) after unit normalisation.
    const auto& g=sample.gravity;
    const auto json=Reply("SensorEvent",{{"gravity",{{"x",-g[1]},{"y",g[0]},{"z",g[2]}}}},0,true).dump();
    SimpleCmd frame;
    // Official H5 module 6718 Ny(cmd,arg,data,arg1,arg2), called by
    // sendHighFqDataToGame(key,data): Ny("HighFqDataToGame",null,data,key).
    frame.cmd="HighFqDataToGame";frame.arg1="SensorEvent";
    frame.data=std::vector<std::uint8_t>(json.begin(),json.end());
    return frame.Serialize();
}

bool GameBridge::complete_login(std::string auth_code,std::int64_t now) {
    if(!login_event_) return false;
    auth_code_=std::move(auth_code);
    if(!enqueue("OnLoginResult",Reply("OnLoginResult",Json::parse(login_info()),
        Json::parse(*login_event_)).dump(),now,login_identity_)) return false;
    login_completed_=true;
    login_event_.reset();
    login_identity_.clear();
    return true;
}

void GameBridge::expire_pending(std::int64_t now) {
    for(auto it=pending_.begin();it!=pending_.end();) {
        if(now>=it->deadline) {
            // An unconfirmed/locally rejected reply is not a completed SDK
            // call. Admit a retransmitted request after the bounded send
            // budget; login will obtain a fresh OAuth code, never reuse this
            // expired reply. Confirmed calls retain the recent dedup entry.
            if(!it->request_identity.empty())
                std::erase(handled_,it->request_identity);
            it=pending_.erase(it);++expired_;continue;
        }
        ++it;
    }
}

void GameBridge::poll(std::int64_t now,
    const std::function<bool(const std::vector<std::uint8_t>&)>& send) {
    expire_pending(now);
    for(auto& pending:pending_) {
        if(now>=pending.retry) {
            const bool accepted=send(pending.bytes);
            pending.retry=now+(accepted?1000:100);
        }
    }
}
} // namespace cloudgenshin::endfield
