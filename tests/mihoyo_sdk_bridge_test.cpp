#include "mihoyo/sdk_bridge.hpp"
#include "mihoyo/combo_login_data.hpp"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

namespace {
using namespace cloudgenshin::sdk;
using Json = nlohmann::json;
struct Sent { std::uint16_t command; std::vector<std::uint8_t> body; };
struct Fixture {
    std::vector<Sent> sent;
    std::vector<std::string> diagnostics;
    bool send_ok = true;
    std::uint32_t incoming_id = 1;
    bool trace_enabled = true;
    Bridge bridge;
    explicit Fixture(BridgeContext context = {},bool enable_trace=true)
        : trace_enabled(enable_trace),bridge(WithEvents(std::move(context)),
        [this](std::uint16_t cmd, const std::vector<std::uint8_t>& bytes) {
            if (!send_ok) return false;
            sent.push_back({cmd, bytes}); return true;
        }) {}
    BridgeContext WithEvents(BridgeContext context) {
        if (context.game_token.empty()) context.game_token = "synthetic-game-token";
        context.event = [this](const char* tag, int value) {
            diagnostics.push_back(std::string(tag) + "=" + std::to_string(value));
        };
        if(trace_enabled) context.trace = [this](const std::string& line) {diagnostics.push_back(line);};
        else context.trace={};
        return context;
    }
    void Receive(const Json& message, std::int64_t now) {
        ReceivePlain(message.dump(),now);
    }
    void ReceivePlain(std::string_view message,std::int64_t now) {
        const auto cipher = EncryptGameDataPayload(message); assert(cipher);
        const auto payload = EncodeSdkGameDataMessage({"SDK", {cipher->begin(), cipher->end()}});
        ReliableMessageQueueData queue;
        queue.msg_id = incoming_id++; queue.msg_type = kRmqMessageNormal;
        queue.seq_id = queue.seq_cnt = 1;
        queue.total_len = queue.data_len = static_cast<std::uint32_t>(payload.size());
        queue.data = payload;
        assert(bridge.HandleProxyPacket(kReliableMessageQueueCmd, EncodeReliableMessageQueueData(queue), now));
    }
    void Invoke(const std::string& function, int index = 1) {
        Receive(Json{{"f", "invoke"}, {"p", Json{{"f", function}, {"p", "{}"}}.dump()}, {"i", index}}, 1000);
    }
    void Webview(const std::string& function, const Json& params, int index) {
        Receive(Json{{"f", "webview"},
                     {"p", Json{{"f", function}, {"p", params}}.dump()},
                     {"i", index}}, 3000);
    }
    void Keep(const Json& params, std::int64_t now) {
        Receive(Json{{"f", "cloud_keep_alive"}, {"p", params.dump()}}, now);
    }
    std::size_t KeepCount() const {
        return std::count_if(sent.begin(), sent.end(), [](const Sent& s) {return s.command == kRtcKeepPlayingCmd;});
    }
    Json LastReply() const {
        for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
            if (it->command != kReliableMessageQueueCmd) continue;
            const auto queue = DecodeReliableMessageQueueData(it->body); assert(queue);
            if (queue->msg_type != kRmqMessageNormal) continue;
            const auto sdk = DecodeSdkGameDataMessage(queue->data); assert(sdk && sdk->name == "SDK");
            const auto plain = DecryptGameDataPayload(std::string(sdk->data.begin(), sdk->data.end())); assert(plain);
            return Json::parse(*plain);
        }
        assert(false); return {};
    }
};
BridgeContext LoginContext() {
    BridgeContext context;
    context.device_id="synthetic-device"; context.app_id="4"; context.channel_id="1";
    context.open_id="123"; context.combo_token="synthetic-combo";
    context.channel_token="synthetic-passport";
    context.sdk_login_data=R"({"combo_id":9223372036854775000,"account_type":1,"heartbeat":true,"asterisk_name":"测***试","guest":false,"nested":{"ok":true}})";
    return context;
}
}
int main() {
    {
        Fixture malformed(LoginContext());
        malformed.ReceivePlain(std::string(4096,'[')+"0"+std::string(4096,']'),0);
        assert(std::find(malformed.diagnostics.begin(),malformed.diagnostics.end(),
            "GAME_SDK_PARSE_FAILED=0")!=malformed.diagnostics.end());
        // A rejected encrypted SDK payload must not poison the receive queue.
        malformed.Invoke("Init",1);assert(malformed.LastReply()["f"]=="on_init_response");
    }
    // Official ys/sr cloudGetData returns cachedValue || "". Only JS-falsy
    // values fall back; empty arrays/objects and structured saves stay intact.
    for(const auto app:{"4","8","12"}) {
        auto context=LoginContext();context.app_id=app;Fixture cache(context);
        const Json values=Json::array({nullptr,false,0,-0.0,"",true,7,"saved",Json::object(),Json::array(),Json{{"server",1}}});
        int index=100;
        for(const auto& value:values) {
            cache.Receive(Json{{"f","cloud_set_data"},{"p",Json{{"saved",value}}},{"i",index++}},1000);
            cache.Receive(Json{{"f","cloud_get_data"},{"p","saved"},{"i",index++}},1001);
            const auto reply=Json::parse(cache.LastReply()["p"].get<std::string>());
            const bool falsy=value.is_null() || (value.is_boolean() && !value.get<bool>()) ||
                (value.is_number() && value.get<double>()==0) || (value.is_string() && value.get_ref<const std::string&>().empty());
            const Json expected=falsy ? Json("") : value.is_structured() ? Json(value.dump()) : value;
            assert(reply["data"]==expected);
        }
    }
    const std::string sample = R"({"f":"cloud_get_data","p":"account_id","i":1})";
    const auto encrypted=EncryptGameDataPayload(sample); assert(encrypted);
    // Independent .NET AES-256/ECB/PKCS7 vector using the public protocol key.
    assert(*encrypted=="5dUCbaevblnI76mIRASJWH84Ayjg1Vo6BUv6QLa6pdo1haiatnqyI0DND0H7xM53");
    assert(DecryptGameDataPayload(*encrypted)==sample);
    assert(!DecryptGameDataPayload("not base64"));
    assert(!DecryptGameDataPayload("AAAAAAAAAAAAAAAAAAAAAA=="));
    Fixture login(LoginContext()); login.Invoke("login_login", 17);
    const auto wire=login.LastReply(); assert(wire["f"]=="on_get_invoke_response");
    const auto callback=Json::parse(wire["p"].get<std::string>()); assert(callback["index"]==17);
    const auto result=Json::parse(callback["data"].get<std::string>());
    const auto data=result["data"]; assert(result["ret"]==0);
    assert(data["app_id"]==4 && data["app_id"].is_number_integer());
    assert(data["channel_id"]==1 && data["channel_id"].is_number_integer());
    assert(data["combo_id"]==9223372036854775000LL && data["account_type"]==1);
    assert(data["heartbeat"]==true && data["guest"]==false && data["nested"]["ok"]==true);
    assert(data["channel_token"]=="synthetic-passport" && data["open_id"]=="123");
    const auto ack=DecodeReliableMessageQueueData(login.sent.back().body);
    assert(ack && ack->msg_type==kRmqMessageAck && ack->data==std::vector<std::uint8_t>({8,1}));
    // Star Rail uses its own numeric app id and SDK metadata over the shared
    // bridge. Never leak the Genshin id or infer online success from a reply.
    for(const auto app:{"4","8","12"}) {
        auto context=LoginContext();context.app_id=app;
        context.device_id=std::string("synthetic-device-")+app;
        context.cps="pc_mihoyo";
        Fixture game(context);game.Invoke("login_login",17);
        const auto p=Json::parse(game.LastReply()["p"].get<std::string>());
        const auto reply=Json::parse(p["data"].get<std::string>());
        assert(reply["ret"]==0 && reply["data"]["app_id"]==std::stoi(app));
        assert(reply["data"]["device_id"]==context.device_id);
        assert(reply["data"]["channel_id"].is_number_integer());
        assert(reply["data"]["combo_id"]==9223372036854775000LL);
        // Official hkrpg combo-web omits host-managed heartbeat/fatigue
        // fields. Other compatibility profiles retain their existing shape.
        assert(reply["data"].contains("heartbeat")== (std::string_view(app)!="8"));
        game.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","info_get_cps"},{"p","{}"}}.dump()},{"i",18}},2000);
        const auto cps=Json::parse(game.LastReply()["p"].get<std::string>());
        assert(cps["index"]==18 && cps["data"]=="pc_mihoyo");
        game.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","info_get_device_id"},{"p","{}"}}.dump()},{"i",19}},2000);
        const auto device=Json::parse(game.LastReply()["p"].get<std::string>());
        assert(device["index"]==19 && device["data"]==context.device_id);
        for(const auto& event:game.diagnostics) {
            assert(event.find(context.device_id)==std::string::npos);
            assert(event.find(context.channel_token)==std::string::npos);
        }
    }
    {
        auto context=LoginContext();context.app_id="8";Fixture traced(context);
        const auto report=[&](const char* message) {
            traced.Receive(Json{{"f","invoke"},{"p",Json{{"f","report_report_event"},
                {"p",Json{{"c_body",{{"message",message}}}}.dump()}}.dump()}},2000);
        };
        for(int i=0;i<80;++i) report("benign synthetic-secret");
        report("1001_2 synthetic-secret");report("1001_2 synthetic-secret");
        assert(std::count(traced.diagnostics.begin(),traced.diagnostics.end(),
            "GAME_SDK_REPORT_NET_1001_2=1")==1);
    }
    assert(std::find(login.diagnostics.begin(), login.diagnostics.end(),
                     "GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT=1")!=login.diagnostics.end());
    for (const auto& line : login.diagnostics) {
        assert(line.find("synthetic-passport")==std::string::npos);
        assert(line.find("synthetic-combo")==std::string::npos);
        assert(line.find("测***试")==std::string::npos);
    }
    for(const auto app:{"4","8","12"}) {
        auto context=LoginContext();context.app_id=app;
        Fixture normal(context,false);
        const auto report=[&](const std::string& message,int retcode=0) {
            normal.Receive(Json{{"f","invoke"},{"p",Json{{"f","report_report_event"},
                {"p",Json{{"c_body",{{"message",message},{"retcode",retcode}}}}.dump()}}.dump()}},2000);
        };
        for(int i=0;i<32;++i) report("benign synthetic-secret");
        report("1001_2 synthetic-secret");report("1001_2 synthetic-secret");
        assert(std::count(normal.diagnostics.begin(),normal.diagnostics.end(),
            "GAME_SDK_REPORT_NET_1001_2=1")==(std::string_view(app)=="8"?1:0));
        report("benign synthetic-secret",1001);report("benign synthetic-secret",1002);
        assert(std::count(normal.diagnostics.begin(),normal.diagnostics.end(),
            "GAME_SDK_REPORT_FIRST_NONZERO_RETCODE=1001")== (std::string_view(app)=="8"?1:0));
        assert(std::find(normal.diagnostics.begin(),normal.diagnostics.end(),
            "GAME_SDK_REPORT_FIRST_NONZERO_RETCODE=1002")==normal.diagnostics.end());
        for(const auto& event:normal.diagnostics) {
            assert(event.find("synthetic-secret")==std::string::npos);
            assert(event.find("report schema=")==std::string::npos);
            assert(event.find("GAME_SDK_REPORT_RETCODE")==std::string::npos);
        }
    }
    login.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","login_get_asterisk_name"},{"p","{}"}}.dump()},{"i",18}},2000);
    const auto name_reply=Json::parse(login.LastReply()["p"].get<std::string>());
    assert(name_reply["index"]==18 && name_reply["data"]=="测***试");
    login.Invoke("Init",19);
    assert(login.LastReply()["f"]=="on_init_response");
    // Pin the legacy local fallback, not an official fingerprint or a proven
    // fix for Star Rail's game-side connection dialog.
    login.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","info_get_device_fingerprint"},{"p","{}"}}.dump()},{"i",20}},2000);
    const auto fp_reply=Json::parse(login.LastReply()["p"].get<std::string>());
    const std::string fp=fp_reply["data"].get<std::string>();
    assert(fp.size()==13);
    for(const char c:fp) assert((c>='0'&&c<='9')||(c>='a'&&c<='z'));
    login.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","info_get_device_fingerprint"},{"p","{}"}}.dump()},{"i",21}},2000);
    const auto fp_reply2=Json::parse(login.LastReply()["p"].get<std::string>());
    assert(fp_reply2["data"].get<std::string>()==fp);
    auto sr_fp_context=LoginContext();sr_fp_context.app_id="8";
    Fixture sr_no_host_fp(sr_fp_context);
    sr_no_host_fp.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","info_get_device_fingerprint"},
        {"p","{}"}}.dump()},{"i",22}},2000);
    const auto sr_fp_reply=Json::parse(sr_no_host_fp.LastReply()["p"].get<std::string>());
    assert(sr_fp_reply["index"]==22 && sr_fp_reply["data"]=="");
    assert(std::find(sr_no_host_fp.diagnostics.begin(),sr_no_host_fp.diagnostics.end(),
        "GAME_SDK_DEVICE_FINGERPRINT_PRESENT=0")!=sr_no_host_fp.diagnostics.end());
    sr_fp_context.device_fingerprint="synthetic-service-fp";
    Fixture sr_service_fp(sr_fp_context);
    sr_service_fp.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","info_get_device_fingerprint"},
        {"p","{}"}}.dump()},{"i",23}},2000);
    const auto issued_fp_reply=Json::parse(sr_service_fp.LastReply()["p"].get<std::string>());
    assert(issued_fp_reply["index"]==23 && issued_fp_reply["data"]=="synthetic-service-fp");
    assert(std::find(sr_service_fp.diagnostics.begin(),sr_service_fp.diagnostics.end(),
        "GAME_SDK_DEVICE_FINGERPRINT_PRESENT=1")!=sr_service_fp.diagnostics.end());
    sr_fp_context.device_fingerprint="bad\r\nvalue";
    Fixture sr_invalid_fp(sr_fp_context);
    sr_invalid_fp.Receive(Json{{"f","invoke_return"},{"p",Json{{"f","info_get_device_fingerprint"},
        {"p","{}"}}.dump()},{"i",24}},2000);
    assert(Json::parse(sr_invalid_fp.LastReply()["p"].get<std::string>())["data"]=="");
    {
        // Official sdkWebview global-UA contract: get starts empty, set stores
        // the game-supplied content and returns undefined (no reply), and a
        // later get echoes the stored value. Malformed values never replace a
        // previously accepted UA.
        Fixture webview(LoginContext());
        webview.Webview("get_global_user_agent", "", 7);
        auto webview_reply=Json::parse(webview.LastReply()["p"].get<std::string>());
        assert(webview_reply["index"]==7 && webview_reply["data"]=="");
        webview.Webview("set_global_user_agent", Json{{"content","Synthetic/1.0 (Switch)"}}, -1);
        const auto after_set=DecodeReliableMessageQueueData(webview.sent.back().body);
        assert(after_set && after_set->msg_type==kRmqMessageAck);
        webview.Webview("get_global_user_agent", "", 8);
        webview_reply=Json::parse(webview.LastReply()["p"].get<std::string>());
        assert(webview_reply["index"]==8 && webview_reply["data"]=="Synthetic/1.0 (Switch)");
        webview.Webview("set_global_user_agent", Json{{"content",std::string(1025,'A')}}, -1);
        webview.Webview("get_global_user_agent", "", 9);
        webview_reply=Json::parse(webview.LastReply()["p"].get<std::string>());
        assert(webview_reply["data"]=="Synthetic/1.0 (Switch)");
        webview.Webview("set_global_user_agent", Json{{"content","bad\nua"}}, -1);
        webview.Webview("get_global_user_agent", "", 10);
        webview_reply=Json::parse(webview.LastReply()["p"].get<std::string>());
        assert(webview_reply["data"]=="Synthetic/1.0 (Switch)");
        webview.Webview("set_global_user_agent", Json{{"content",""}}, -1);
        webview.Webview("get_global_user_agent", "", 11);
        webview_reply=Json::parse(webview.LastReply()["p"].get<std::string>());
        assert(webview_reply["data"]=="");
        webview.Webview("unknown_target", "", 12);
        webview_reply=Json::parse(webview.LastReply()["p"].get<std::string>());
        assert(webview_reply["index"]==12 && webview_reply["data"]=="{}");
    }
    bool ended=false; auto end_context=LoginContext(); end_context.on_session_end=[&]{ended=true;};
    Fixture logout(end_context); logout.Invoke("login_logout"); assert(ended);
    auto legacy_context=LoginContext(); legacy_context.sdk_login_data.clear(); legacy_context.channel_token.clear();
    Fixture legacy(legacy_context); legacy.Invoke("login_login");
    const auto legacy_p=Json::parse(legacy.LastReply()["p"].get<std::string>());
    assert(!Json::parse(legacy_p["data"].get<std::string>())["data"].contains("channel_token"));
    auto first_party=legacy_context;first_party.account_id=first_party.open_id;
    Fixture compat(first_party);compat.Invoke("login_login");
    const auto compat_p=Json::parse(compat.LastReply()["p"].get<std::string>());
    const auto compat_data=Json::parse(compat_p["data"].get<std::string>())["data"];
    assert(compat_data["account_type"]==1 && compat_data["combo_token"]=="synthetic-combo");
    assert(!compat_data.contains("channel_token") && !compat_data.contains("combo_id"));
    assert(std::find(compat.diagnostics.begin(),compat.diagnostics.end(),"GAME_SDK_LOGIN_LEGACY_ACCOUNT_TYPE_RESOLVED=1")!=compat.diagnostics.end());
    for(const auto app:{"8","12","04",""}) {
        auto untouched=Json::object();
        assert(!cloudgenshin::auth::ResolveLegacyGenshinAccountType(untouched,app,"1","123","123") && untouched.empty());
    }
    for(const auto account:{"", "0", "0123", "abc", "9223372036854775808"}) {
        auto untouched=Json::object();
        assert(!cloudgenshin::auth::ResolveLegacyGenshinAccountType(untouched,"4","1",account,account) && untouched.empty());
    }
    auto untouched=Json::object();
    assert(!cloudgenshin::auth::ResolveLegacyGenshinAccountType(untouched,"4","2","123","123"));
    assert(!cloudgenshin::auth::ResolveLegacyGenshinAccountType(untouched,"4","1","123","456"));
    untouched=Json{{"account_type",0}};
    assert(!cloudgenshin::auth::ResolveLegacyGenshinAccountType(untouched,"4","1","123","123") && untouched["account_type"]==0);
    auto bad_context=LoginContext(); bad_context.sdk_login_data=R"({"open_id":"wrong-account"})";
    Fixture bad(bad_context); bad.Invoke("login_login");
    const auto bad_p=Json::parse(bad.LastReply()["p"].get<std::string>());
    assert(Json::parse(bad_p["data"].get<std::string>())["ret"]!=0);
    for(const auto* invalid:{"", "0", "1suffix", "-1", "2147483648", "4294967296"}) {
        auto wrong_id=LoginContext();wrong_id.app_id=invalid;
        Fixture invalid_app(wrong_id);invalid_app.Invoke("login_login");
        const auto response=Json::parse(invalid_app.LastReply()["p"].get<std::string>());
        assert(Json::parse(response["data"].get<std::string>())["ret"]!=0);
        wrong_id=LoginContext();wrong_id.channel_id=invalid;
        Fixture invalid_channel(wrong_id);invalid_channel.Invoke("login_login");
        const auto channel=Json::parse(invalid_channel.LastReply()["p"].get<std::string>());
        assert(Json::parse(channel["data"].get<std::string>())["ret"]!=0);
    }

    Fixture finite; finite.Keep(Json{{"enable",true},{"duration",2}},1000);
    assert(finite.KeepCount()==1); // immediate, not after 2 seconds
    finite.bridge.Poll(60999); assert(finite.KeepCount()==1);
    finite.bridge.Poll(61000); assert(finite.KeepCount()==2);
    finite.bridge.Poll(121000); assert(finite.KeepCount()==3);
    finite.bridge.Poll(181000); assert(finite.KeepCount()==3); // stops
    finite.Keep(Json{{"enable",true},{"duration",100}},200000);
    finite.Keep(Json{{"enable",false}},200001);
    const auto stopped=finite.KeepCount(); finite.bridge.Poll(500000); assert(finite.KeepCount()==stopped);
    finite.Keep(Json{{"enable",true}},600000); // no known box-config default
    const auto fallback=finite.KeepCount(); finite.bridge.Poll(1000000); assert(finite.KeepCount()==fallback);
    finite.Keep(Json{{"enable",true},{"duration",1e300}},1100000);
    finite.bridge.Poll(1200000); assert(finite.KeepCount()==fallback);
    auto configured=LoginContext(); configured.keep_alive_duration_minutes=5; configured.idle_timeout_minutes=3;
    Fixture budget(configured); budget.Keep(Json{{"enable",true}},1000);
    budget.bridge.Poll(61000); budget.bridge.Poll(121000); budget.bridge.Poll(181000);
    assert(budget.KeepCount()==3); // duration - user idle threshold + 1
    Fixture idle; idle.bridge.OnLocalInput(0); idle.bridge.OnLocalInput(1);
    assert(idle.KeepCount()==1); // timestamp zero must still be throttled
    assert(idle.bridge.HandleProxyPacket(kRtcNotPlayingTipsCmd,{},2));
    idle.send_ok=false; idle.bridge.OnLocalInput(3);
    idle.send_ok=true; idle.bridge.OnLocalInput(4); assert(idle.KeepCount()==2);
    idle.bridge.OnLocalInput(5); assert(idle.KeepCount()==2);
    assert(!cloudgenshin::auth::DecodeComboSdkData("[]"));
    assert(!cloudgenshin::auth::DecodeComboSdkData(std::string(4097,'x')));
    assert(!cloudgenshin::auth::DecodeComboSdkData(R"({"heartbeat":"true"})"));
    assert(!cloudgenshin::auth::DecodeComboSdkData(R"({"account_type":-1})"));
    assert(!cloudgenshin::auth::DecodeComboSdkData(R"({"combo_id":1.5})"));
    assert(!cloudgenshin::auth::DecodeComboSdkData(R"({"asterisk_name":false})"));
    assert(cloudgenshin::auth::DecodeComboSdkData(R"({"combo_id":"9223372036854775000"})"));
    Json deep=Json::object();
    for (int i=0;i<10;++i) deep=Json{{"nested",deep}};
    assert(!cloudgenshin::auth::DecodeComboSdkData(deep.dump()));
}
