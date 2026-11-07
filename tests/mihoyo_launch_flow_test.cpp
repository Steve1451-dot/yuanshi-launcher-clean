#include "mihoyo/launch_flow.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/dispatch_prepare.hpp"
#include "mihoyo/initial_dispatch.hpp"
#include "mihoyo/queue_ticket.hpp"
#include "mihoyo/combo_exchange.hpp"
#include "mihoyo/ping_probe.hpp"
#include "mihoyo/passport_token_resume.hpp"
#include "mihoyo/stream_settings_store.hpp"
#include <filesystem>
#include <borealis/extern/nlohmann/json.hpp>
#include <cassert>
#include <algorithm>
#ifdef CLOUDGENSHIN_TEST_BASE64_SHIM
// MinGW unit tests link only base64.c, not the full TLS library. These helpers
// are used exclusively by the public device-info encoder in this host test.
extern "C" unsigned char mbedtls_ct_base64_enc_char(unsigned char v) {
    return v<26 ? static_cast<unsigned char>('A'+v) : v<52 ? static_cast<unsigned char>('a'+v-26) :
        v<62 ? static_cast<unsigned char>('0'+v-52) : v==62 ? '+' : '/';
}
extern "C" signed char mbedtls_ct_base64_dec_value(unsigned char v) {
    if(v>='A' && v<='Z') return v-'A';
    if(v>='a' && v<='z') return v-'a'+26;
    if(v>='0' && v<='9') return v-'0'+52;
    return v=='+' ? 62 : v=='/' ? 63 : -1;
}
#endif

namespace {
using namespace cloudgenshin;
enum class Scenario { QueueSuccess, LongQueue, CancelQueue, AckFailure, InvalidStartup, ServiceFailure, PingFailure, PingCancelled, NodesRejected,
    QueueTransient, QueueTransportExhausted, QueueTlsFailure, QueueRetryCancelled, InitialTransportFailure, ServiceTransportFailure };
enum class LoginScenario { Combo, Account, Risk, VerificationRequired, ExchangeRejected, CancelExchange };
enum class ResumeScenario { Success, ExchangeRejected, Risk, Verification, Network, Http, Malformed,
    WrongAccount, WrongGame, WrongChannel, WrongOpenId, CloudRejected, CloudNetwork,
    CancelInitial, CancelExchange, CancelCloud };
std::optional<ResumeScenario> resume_scenario;
unsigned resume_checks=0;
auth::GamerLoginStatus resume_initial_status=auth::GamerLoginStatus::Rejected;
int resume_initial_code=-100;
LoginScenario login_scenario=LoginScenario::Combo;
const GameProfile* expected_profile=nullptr;
unsigned exchanges=0,cloud_logins=0,queries=0,qr_fetches=0;
auth::GamerLoginStatus gamer_status=auth::GamerLoginStatus::Success;
int gamer_retcode=-100;
std::string login_device;
Scenario scenario=Scenario::QueueSuccess;
std::atomic_bool cancelled{false};
unsigned prepared=0,dispatched=0,polls=0,acks=0,leaves=0;
int initial_position=12;
bool omit_queue_rank=false;
std::vector<std::string> events;
std::vector<int> positions;
std::vector<int> login_network_errors;
unsigned web_resumes=0;
bool web_resume_ok=true,web_resume_wrong_account=false;
std::string settings_home;
int expected_graphics=kStartupGraphicsMode,expected_fps=kStartupStreamFps;
dispatch::DispatchResponse Finished() {
    dispatch::DispatchResponse result;
    result.status=dispatch::DispatchStatus::Finished;result.requires_ticket_ack=true;
    dispatch::SdkStartGameParams params;
    params.sid="test-session";params.pod_id="test-pod";params.game_token="test-game-token";
    params.game_server_wss_url="wss://cloud.invalid/game";
    if(scenario!=Scenario::InvalidStartup) result.sdk_start_game_params=params;
    return result;
}
void Reset(Scenario value) {
    scenario=value;cancelled=false;prepared=dispatched=polls=acks=leaves=0;
    initial_position=12;omit_queue_rank=false;events.clear();positions.clear();
}
} // namespace

namespace opennow {
const std::string& AppHomePath() {return settings_home;}
}

namespace cloudgenshin {
std::optional<Authorization> ResumePassportFromAccount(const GameProfile& game,const Authorization& saved,
    const std::atomic_bool& stop,const Progress&,PassportResumeSource source) {
    ++web_resumes;assert(game.id=="hkrpg" && saved.passport_mid=="synthetic-mid" && !stop);
    assert(source==PassportResumeSource::SToken);
    if(!web_resume_ok) return {};
    auto candidate=saved;candidate.combo.combo_token="refreshed-web-combo";
    candidate.channel_token="refreshed-web-stoken";
    if(web_resume_wrong_account) candidate.account_id="456";
    return candidate;
}
}

namespace cloudgenshin::auth {
QrResult FetchQr(const GameProfile& profile,const std::string& device,const std::atomic_bool*) {
    ++qr_fetches;
    assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    assert(device.size()==32);
    login_device=device;
    QrResult result;result.status=QrStatus::Ready;
    result.challenge=QrChallenge{"https://user.mihoyo.com/synthetic","test-qr-ticket"};return result;
}
QrResult QueryQr(const GameProfile& profile,const std::string& device,const std::string& ticket,const std::atomic_bool*) {
    assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    assert(device==login_device && ticket=="test-qr-ticket");++queries;
    QrResult result;result.status=QrStatus::Confirmed;
    if(login_scenario==LoginScenario::Risk) {result.status=QrStatus::RiskRejected;result.retcode=-3503;return result;}
    if(login_scenario==LoginScenario::VerificationRequired) {
        result.status=QrStatus::VerificationRequired;result.retcode=-3101;result.verification_header_present=true;return result;
    }
    if(login_scenario!=LoginScenario::Combo) {
        result.account_authorization=AccountLoginFields{"123","test-account-token"};result.account_id="123";return result;
    }
    result.authorization=ComboLoginFields{std::to_string(expected_profile->app_id),"1","test-open","test-combo-token"};
    result.account_id="test-aid";return result;
}
GamerLoginResult CheckGamerLogin(const GameProfile& profile,const GamerLoginRequest& request,const std::atomic_bool*) {
    ++cloud_logins;
    assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    assert(expected_profile);
    const std::string expected_biz_header="x-rpc-cg_game_biz: "+std::string(expected_profile->game_biz);
    assert(std::find(request.headers.begin(),request.headers.end(),expected_biz_header)!=request.headers.end());
    if(resume_scenario) {
        if(++resume_checks==1) {
            if(*resume_scenario==ResumeScenario::CancelInitial) cancelled=true;
            return {resume_initial_status,200,resume_initial_code};
        }
        assert(resume_checks==2); // Never retry/re-dispatch in a loop.
        if(*resume_scenario==ResumeScenario::CancelCloud) cancelled=true;
        if(*resume_scenario==ResumeScenario::CloudRejected) return {GamerLoginStatus::Rejected,200,-100};
        if(*resume_scenario==ResumeScenario::CloudNetwork) return {GamerLoginStatus::TransportError,0,0,28};
        return {GamerLoginStatus::Success,200,0};
    }
    if(gamer_status==GamerLoginStatus::TransportError) return {gamer_status,0,0,28};
    return {gamer_status,200,gamer_status==GamerLoginStatus::Success ? 0 : gamer_retcode};
}
QrResult ExchangeAccountForCombo(const GameProfile& profile,const ComboExchangeRequest& request,const std::atomic_bool*) {
    ++exchanges;
    assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    assert(request.device==login_device);
    QrResult result;
    if(resume_scenario) {
        const auto body=nlohmann::json::parse(request.body);
        const auto data=nlohmann::json::parse(body["data"].get<std::string>());
        assert(body["app_id"]==profile.app_id && data["uid"]=="123" && data["token"]=="saved-account-token");
        result.http_status=200;
        switch(*resume_scenario) {
        case ResumeScenario::ExchangeRejected:result.status=QrStatus::Rejected;result.retcode=-100;return result;
        case ResumeScenario::Risk:result.status=QrStatus::RiskRejected;result.retcode=-3503;return result;
        case ResumeScenario::Verification:result.status=QrStatus::VerificationRequired;result.retcode=-3101;return result;
        case ResumeScenario::Network:result.status=QrStatus::TransportError;return result;
        case ResumeScenario::Http:result.status=QrStatus::HttpError;result.http_status=503;return result;
        case ResumeScenario::Malformed:result.status=QrStatus::Confirmed;return result;
        default:break;
        }
        if(*resume_scenario==ResumeScenario::CancelExchange) cancelled=true;
        result.status=QrStatus::Confirmed;
        result.account_id=*resume_scenario==ResumeScenario::WrongAccount ? "456" : "123";
        result.authorization=ComboLoginFields{
            *resume_scenario==ResumeScenario::WrongGame ? "999" : std::to_string(profile.app_id),
            *resume_scenario==ResumeScenario::WrongChannel ? "2" : "1",
            *resume_scenario==ResumeScenario::WrongOpenId ? "456" : "123","refreshed-combo-token"};
        return result;
    }
    if(login_scenario==LoginScenario::ExchangeRejected) {result.status=QrStatus::Rejected;result.retcode=-100;return result;}
    if(login_scenario==LoginScenario::CancelExchange) cancelled=true;
    result.status=QrStatus::Confirmed;
    result.authorization=ComboLoginFields{std::to_string(expected_profile->app_id),"1","123","test-combo-token"};
    return result;
}
}
namespace cloudgenshin::dispatch {
PrepareResult SubmitPrepare(const GameProfile& profile,const PrepareRequest& request,const std::atomic_bool*) {
    ++prepared;assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    PrepareResult result;result.http_status=200;
    if(scenario==Scenario::ServiceFailure) {result.status=PrepareStatus::Maintenance;return result;}
    if(scenario==Scenario::ServiceTransportFailure) {
        result.status=PrepareStatus::TransportError;result.transport_error=28;result.http_status=0;
        result.transport_diagnostics={.captured=true,.requires_tls=true,.lookup_ms=1,.connect_ms=2,.total_ms=10000};
        return result;
    }
    assert(prepared==static_cast<unsigned>(request.operation)+1);
    result.status=PrepareStatus::Success;
    if(request.operation==PrepareOperation::PingServers) result.ping_sdk_param="CgIIEQ==";
    if(request.operation==PrepareOperation::Nodes) {
        const auto data=nlohmann::json::parse(request.body);
        const auto ping=nlohmann::json::parse(data["node"].get<std::string>());
        assert(ping["ping_results"][0]["rtt"]==24 && data["speed_client_type"]==7);
        if(scenario==Scenario::NodesRejected) {result.status=PrepareStatus::Rejected;result.retcode=-110013;return result;}
    }
    if(request.operation==PrepareOperation::Nodes) result.nodes={{"test-node","[\"r1\"]",true}};
    return result;
}
PingProbeResult ProbePingServers(const std::vector<PingServer>& servers,const std::atomic_bool&) {
    assert(servers.size()==1 && servers[0].id==17);
    if(scenario==Scenario::PingFailure) return {};
    if(scenario==Scenario::PingCancelled) {cancelled=true;return {};}
    PingProbeResult result;result.tested=result.succeeded=1;
    result.node_json=*BuildMeasuredPingNode({{servers[0],24}});return result;
}
DispatchResponse SubmitInitialDispatch(const GameProfile& profile,const InitialDispatchRequest& request,const std::atomic_bool*) {
    ++dispatched;
    assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    const auto body=nlohmann::json::parse(request.body);
    assert(expected_profile && body["biz_key"]==std::string(expected_profile->game_biz) &&
        body["queue_type"]=="" && body["hint"].is_null());
    assert(body["net_state"]==4 && body["env"]=="2");
    assert(body["fps"]==expected_fps);
    assert(body["bit_rate"]==MihoyoBitrate(StreamQualityBitrate(expected_graphics)->maximum));
    const auto user_data=nlohmann::json::parse(body["user_data"].get<std::string>());
    if(profile.id=="hk4e" || profile.id=="hkrpg")
        assert(user_data["idg"]==true);
    else assert(!user_data.contains("idg"));
    assert(body["using_new_cmd_line"]==true && body["cmd_line"]=="");
    DispatchResponse result;result.status=DispatchStatus::Queued;result.ticket="test-queue-ticket";
    if(scenario==Scenario::InitialTransportFailure) {
        result={};result.status=DispatchStatus::TransportError;
        result.transport_error=28;result.retryable_transport=true;return result;
    }
    result.query_interval_seconds=scenario==Scenario::LongQueue ? 0 : 1;
    result.branch_queue_length=439;
    result.queue_length=480;
    if(!omit_queue_rank) result.queue_rank=initial_position;
    if(scenario==Scenario::CancelQueue) cancelled=true;
    return result;
}
DispatchResponse PollQueue(const GameProfile& profile,const QueueRequest& request,const std::atomic_bool*) {
    ++polls;assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    assert(request.operation==QueueOperation::Poll);
    assert(nlohmann::json::parse(request.body)["ticket"]=="test-queue-ticket");
    if(scenario==Scenario::LongQueue && polls==1) {
        DispatchResponse queued;queued.status=DispatchStatus::Queued;
        queued.branch_queue_length=450;
        queued.queue_length=500;
        if(!omit_queue_rank) queued.queue_rank=initial_position+1;
        queued.query_interval_seconds=0;queued.http_status=200;
        return queued;
    }
    if((scenario==Scenario::QueueTransient && polls==1) ||
        scenario==Scenario::QueueTransportExhausted || scenario==Scenario::QueueTlsFailure ||
        scenario==Scenario::QueueRetryCancelled) {
        DispatchResponse r;r.status=DispatchStatus::TransportError;
        r.transport_error=scenario==Scenario::QueueTlsFailure ? 60 : 28;
        r.retryable_transport=scenario!=Scenario::QueueTlsFailure;
        if(scenario==Scenario::QueueRetryCancelled) cancelled=true;
        return r;
    }
    return Finished();
}
QueueActionResult SubmitQueueAction(const GameProfile& profile,const QueueRequest& request,const std::atomic_bool*,long timeout_ms) {
    assert(expected_profile && profile.game_biz==expected_profile->game_biz);
    if(request.operation==QueueOperation::Leave) {++leaves;assert(timeout_ms==5000);return {QueueActionStatus::Success,200,0};}
    assert(request.operation==QueueOperation::Acknowledge);++acks;
    return {scenario==Scenario::AckFailure ? QueueActionStatus::ApiRejected : QueueActionStatus::Success,200,0};
}
}

int main() {
    using namespace cloudgenshin;
    const auto directory=std::filesystem::temp_directory_path()/
        ("launch-settings-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);settings_home=directory.string();
    // The launch flow is exercised with the default production profile; the
    // mocks above pin every request to the active game identity.
    const auto& profile=DefaultGameProfile();
    expected_profile=&profile;
    const Progress progress=[](const std::string& name,int code,long) {
        assert(name.find("test-combo-token")==name.npos && name.find("test-queue-ticket")==name.npos && name.find("https:")==name.npos);
        events.push_back(name);
        if(name=="SAVED_LOGIN_NETWORK_ERROR") login_network_errors.push_back(code);
        if(name=="QUEUED_POSITION") positions.push_back(code);
    };
    auto authorization=Login(profile,cancelled,[](const std::string&){},progress);
    assert(authorization && authorization->account_id=="test-aid");
    assert(exchanges==0 && cloud_logins==1);
    login_scenario=LoginScenario::Account;
    auto account_login=Login(profile,cancelled,[](const std::string&){},progress);
    assert(account_login && account_login->account_id=="123" && exchanges==1 && cloud_logins==2);
    login_scenario=LoginScenario::Risk;queries=0;events.clear();
    assert(!Login(profile,cancelled,[](const std::string&){},progress) && queries==1 && exchanges==1 && cloud_logins==2);
    assert(events.back()=="QR_OFFICIAL_RISK_REJECTED");
    login_scenario=LoginScenario::VerificationRequired;events.clear();
    assert(!Login(profile,cancelled,[](const std::string&){},progress) && exchanges==1 && cloud_logins==2);
    assert(events.back()=="QR_OFFICIAL_VERIFICATION_REQUIRED");
    assert(std::find(events.begin(),events.end(),"QR_AIGIS_HEADER_PRESENT")!=events.end());
    login_scenario=LoginScenario::ExchangeRejected;
    assert(!Login(profile,cancelled,[](const std::string&){},progress) && exchanges==2 && cloud_logins==2);
    assert(events.back()=="COMBO_EXCHANGE_FAILED");
    login_scenario=LoginScenario::CancelExchange;
    assert(!Login(profile,cancelled,[](const std::string&){},progress) && exchanges==3 && cloud_logins==2);
    cancelled=false;login_scenario=LoginScenario::Combo;
    Reset(Scenario::QueueSuccess);
    auto result=Launch(profile,*authorization,cancelled,progress);
    assert(result && prepared==4 && dispatched==1 && polls==1 && acks==1 && leaves==0);
    // A saved low-quality/30 FPS choice overrides high/60 in the signed
    // dispatch, before a pod is allocated or an overlay is constructed.
    const auto settings_path=overlay::StreamSettingsPath(settings_home,profile.id);
    assert(overlay::SaveSavedSettings(settings_path,2,30));
    expected_graphics=2;expected_fps=30;Reset(Scenario::QueueSuccess);
    assert(Launch(profile,*authorization,cancelled,progress));
    std::filesystem::remove(settings_path);
    expected_graphics=kStartupGraphicsMode;expected_fps=kStartupStreamFps;
    static_assert(QueueWaitLimit({}).count()==120);
    static_assert(QueueWaitLimit({true}).count()==30);
    // Queue size and rank are different in the APK. Large totals must not
    // eject a short personal queue; missing rank must stay unknown, not 439.
    Reset(Scenario::LongQueue);initial_position=2;
    result=Launch(profile,*authorization,cancelled,progress);
    assert(result && polls==2 && acks==1 && leaves==0);
    assert((positions==std::vector<int>{2,3}));
    Reset(Scenario::LongQueue);omit_queue_rank=true;
    result=Launch(profile,*authorization,cancelled,progress);
    assert(result && polls==2 && acks==1 && leaves==0);
    assert((positions==std::vector<int>{-1,-1}));
    Reset(Scenario::LongQueue);initial_position=422;
    result=Launch(profile,*authorization,cancelled,progress,{true});
    assert(result && dispatched==1 && polls==2 && acks==1 && leaves==0);
    assert(std::count(events.begin(),events.end(),"AUTOTEST_LONG_QUEUE_WAIT_ENABLED")==1);
    assert(std::count(events.begin(),events.end(),"QUEUE_ABANDONED_TOO_LONG")==0);
    Reset(Scenario::CancelQueue);initial_position=422;
    result=Launch(profile,*authorization,cancelled,progress,{true});
    assert(!result && dispatched==1 && polls==0 && acks==0 && leaves==1);
    // Preserve the user's default >100 exit policy. Exactly 100 may wait;
    // both an initially long queue and a later increase must explain the
    // decision, leave once and never allocate another pod or paid queue.
    Reset(Scenario::QueueSuccess);initial_position=100;
    result=Launch(profile,*authorization,cancelled,progress);
    assert(result && polls==1 && acks==1 && leaves==0);
    Reset(Scenario::LongQueue);initial_position=99;
    result=Launch(profile,*authorization,cancelled,progress);
    assert(result && polls==2 && acks==1 && leaves==0);
    for(const int position:{100,101,233,243,327}) {
        Reset(Scenario::LongQueue);initial_position=position;
        result=Launch(profile,*authorization,cancelled,progress);
        assert(!result && prepared==4 && dispatched==1 && acks==0 && leaves==1);
        assert(polls==static_cast<unsigned>(position==100 ? 1 : 0));
        assert(std::count(events.begin(),events.end(),"QUEUE_ABANDONED_TOO_LONG")==1);
        assert(std::find(events.begin(),events.end(),"LAUNCH_CANCELLED")==events.end());
        assert(events.back()=="QUEUE_LEFT");
    }
    Reset(Scenario::QueueTransient);result=Launch(profile,*authorization,cancelled,progress);
    assert(result && dispatched==1 && polls==2 && acks==1 && leaves==0);
    assert(std::count(events.begin(),events.end(),"QUEUE_POLL_NETWORK_RETRY")==1);
    Reset(Scenario::QueueTransportExhausted);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && dispatched==1 && polls==3 && acks==0 && leaves==1);
    assert(std::count(events.begin(),events.end(),"QUEUE_POLL_NETWORK_RETRY")==2);
    Reset(Scenario::QueueTlsFailure);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && dispatched==1 && polls==1 && acks==0 && leaves==1);
    assert(std::count(events.begin(),events.end(),"QUEUE_POLL_NETWORK_RETRY")==0);
    Reset(Scenario::InitialTransportFailure);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && dispatched==1 && polls==0 && acks==0 && leaves==0);
    assert(std::count(events.begin(),events.end(),"QUEUE_POLL_NETWORK_RETRY")==0);
    Reset(Scenario::QueueRetryCancelled);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && dispatched==1 && polls==1 && acks==0 && leaves==1);
    Reset(Scenario::CancelQueue);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && leaves==1 && polls==0 && acks==0);
    Reset(Scenario::AckFailure);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && leaves==1 && polls==1 && acks==1);
    Reset(Scenario::InvalidStartup);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && leaves==1 && acks==0);
    Reset(Scenario::ServiceFailure);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && prepared==1 && dispatched==0);
    Reset(Scenario::ServiceTransportFailure);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && prepared==1 && dispatched==0 && events.back()=="SERVICE_CHECK_FAILED");
    assert(std::count(events.begin(),events.end(),"DISPATCH_PREPARE_NETWORK_ERROR")==1);
    assert(std::count(events.begin(),events.end(),"HTTP_FAILURE_PHASE")==1);
    assert(std::count(events.begin(),events.end(),"HTTP_FAILURE_TOTAL_MS")==1);
    Reset(Scenario::PingFailure);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && prepared==2 && dispatched==0 && events.back()=="PING_ALL_FAILED_NO_NODE_REQUEST");
    Reset(Scenario::PingCancelled);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && prepared==2 && dispatched==0 && events.back()=="PING_CANCELLED");
    Reset(Scenario::NodesRejected);result=Launch(profile,*authorization,cancelled,progress);
    assert(!result && prepared==3 && dispatched==0 && events.back()=="GET_NODES_FAILED");
    Reset(Scenario::QueueSuccess);authorization->account_id.clear();
    assert(!Launch(profile,*authorization,cancelled,progress) && prepared==0 && dispatched==0);
    const unsigned saved_fetches=qr_fetches,saved_queries=queries;
    events.clear();
    assert(ResumeLogin(profile,*account_login,cancelled,progress)==ResumeStatus::Valid);
    assert(events.back()=="LOGIN_RESTORED" && qr_fetches==saved_fetches && queries==saved_queries);
    gamer_status=auth::GamerLoginStatus::Rejected;
    assert(ResumeLogin(profile,*account_login,cancelled,progress)==ResumeStatus::Rejected);
    assert(events.back()=="SAVED_LOGIN_REJECTED" && qr_fetches==saved_fetches && queries==saved_queries);
    gamer_status=auth::GamerLoginStatus::TransportError;
    assert(ResumeLogin(profile,*account_login,cancelled,progress)==ResumeStatus::RetryLater);
    assert(events.back()=="SAVED_LOGIN_RETRY_LATER" && qr_fetches==saved_fetches && queries==saved_queries);
    assert(login_network_errors==std::vector<int>{28});
    const unsigned checked_logins=cloud_logins;
    cancelled=true;
    assert(ResumeLogin(profile,*account_login,cancelled,progress)==ResumeStatus::Cancelled);
    assert(cloud_logins==checked_logins && qr_fetches==saved_fetches && queries==saved_queries);
    cancelled=true;assert(!Login(profile,cancelled,[](const std::string&){},progress));

    // The same QR -> Combo -> game login -> dispatch chain must stay profile
    // scoped for the two newer games. The mocks verify the outbound
    // game_biz/app_id on every request; no live network is used.
    for (const char* id : {"hkrpg"}) {
        const auto* game=FindGameProfile(id);
        assert(game);
        expected_profile=game;
        cancelled=false;
        gamer_status=auth::GamerLoginStatus::Success;
        login_scenario=LoginScenario::Combo;
        Reset(Scenario::QueueSuccess);
        events.clear();
        const unsigned before_fetches=qr_fetches,before_queries=queries,before_logins=cloud_logins;
        auto game_login=Login(*game,cancelled,[](const std::string&){},progress);
        assert(game_login && game_login->account_id=="test-aid");
        assert(qr_fetches==before_fetches+1 && queries==before_queries+1 &&
            cloud_logins==before_logins+1);
        auto game_launch=Launch(*game,*game_login,cancelled,progress);
        assert(game_launch && prepared==4 && dispatched==1 && polls==1 && acks==1 && leaves==0);
    }

    // nap's -100002 service gate must surface as an eligibility event, not a
    // generic transport failure. The outbound request is unchanged.
    expected_profile=&profile;
    gamer_status=auth::GamerLoginStatus::Rejected;
    gamer_retcode=-100002;
    cancelled=false;login_scenario=LoginScenario::Combo;events.clear();
    assert(!Login(profile,cancelled,[](const std::string&){},progress));
    assert(events.back()=="CLOUD_AUTH_SERVICE_UNAVAILABLE");
    gamer_status=auth::GamerLoginStatus::Success;
    gamer_retcode=-100;

    // A saved account token may renew an expired per-game Combo, but the
    // replacement cannot escape before same-account validation succeeds.
    for(const char* id:{"hk4e","hkrpg"}) {
        expected_profile=FindGameProfile(id);assert(expected_profile);
        const auto& game=*expected_profile;
        login_device=std::string(32,'a');
        const Authorization saved{{std::to_string(game.app_id),"1","123","expired-combo-token"},
            NativeWebIdentity(game,login_device),"123","saved-account-token"};
        const auto exercise=[&](ResumeScenario scenario,ResumeStatus expected,unsigned checks,bool exchanged) {
            resume_scenario=scenario;resume_checks=0;cancelled=false;events.clear();
            std::optional<Authorization> refreshed;
            const auto before_exchanges=exchanges,before_logins=cloud_logins,before_qr=qr_fetches,before_query=queries;
            assert(ResumeLogin(game,saved,cancelled,progress,&refreshed)==expected);
            assert(cloud_logins-before_logins==checks && exchanges-before_exchanges==(exchanged?1u:0u));
            assert(qr_fetches==before_qr && queries==before_query && saved.combo.combo_token=="expired-combo-token");
            if(expected==ResumeStatus::Valid) {
                assert(refreshed && refreshed->combo.combo_token=="refreshed-combo-token");
                assert(refreshed->device.device_id==saved.device.device_id && refreshed->account_id==saved.account_id);
                assert(refreshed->channel_token==saved.channel_token && refreshed->combo.app_id==saved.combo.app_id);
                assert(events.back()=="LOGIN_RESTORED");
                assert(std::find(events.begin(),events.end(),"SAVED_LOGIN_REJECTED")==events.end());
            } else assert(!refreshed);
        };
        exercise(ResumeScenario::Success,ResumeStatus::Valid,2,true);
        for(auto value:{ResumeScenario::ExchangeRejected,ResumeScenario::Risk,ResumeScenario::Verification,
                       ResumeScenario::WrongAccount,ResumeScenario::WrongGame,ResumeScenario::WrongChannel,
                       ResumeScenario::WrongOpenId}) exercise(value,ResumeStatus::Rejected,1,true);
        for(auto value:{ResumeScenario::Network,ResumeScenario::Http,ResumeScenario::Malformed})
            exercise(value,ResumeStatus::RetryLater,1,true);
        exercise(ResumeScenario::CloudRejected,ResumeStatus::Rejected,2,true);
        exercise(ResumeScenario::CloudNetwork,ResumeStatus::RetryLater,2,true);
        exercise(ResumeScenario::CancelInitial,ResumeStatus::Cancelled,1,false);
        exercise(ResumeScenario::CancelExchange,ResumeStatus::Cancelled,1,true);
        exercise(ResumeScenario::CancelCloud,ResumeStatus::Cancelled,2,true);

        resume_scenario=ResumeScenario::Success;
        for(const int code:{-100002,-3503,-3101}) {
            resume_initial_code=code;
            exercise(ResumeScenario::Success,ResumeStatus::Rejected,1,false);
        }
        resume_initial_code=-100;
        resume_initial_status=auth::GamerLoginStatus::TransportError;
        exercise(ResumeScenario::Success,ResumeStatus::RetryLater,1,false);
        resume_initial_status=auth::GamerLoginStatus::Rejected;
        for(const auto& token:{std::string{},std::string("bad\ntoken")}) {
            auto without_account_token=saved;without_account_token.channel_token=token;
            resume_checks=0;cancelled=false;std::optional<Authorization> refreshed;
            const auto before_exchanges=exchanges;
            assert(ResumeLogin(game,without_account_token,cancelled,progress,&refreshed)==ResumeStatus::Rejected);
            assert(resume_checks==1 && exchanges==before_exchanges && !refreshed);
        }
    }
    {
        expected_profile=FindGameProfile("hkrpg");const auto& game=*expected_profile;
        const Authorization saved{{"8","1","123","expired-web-combo"},NativeWebIdentity(game,std::string(32,'a')),
            "123","saved-web-stoken","synthetic-mid"};
        const auto before=exchanges;
        resume_scenario=ResumeScenario::Success;resume_checks=0;cancelled=false;
        std::optional<Authorization> refreshed;
        assert(ResumeLogin(game,saved,cancelled,progress,&refreshed)==ResumeStatus::Valid);
        assert(web_resumes==1 && resume_checks==2 && exchanges==before && refreshed);
        assert(refreshed->combo.combo_token=="refreshed-web-combo" && refreshed->channel_token=="refreshed-web-stoken");
        assert(refreshed->passport_mid==saved.passport_mid && saved.combo.combo_token=="expired-web-combo");
        web_resume_ok=false;resume_checks=0;refreshed.reset();
        assert(ResumeLogin(game,saved,cancelled,progress,&refreshed)==ResumeStatus::RetryLater);
        assert(web_resumes==2 && resume_checks==1 && exchanges==before && !refreshed);
        web_resume_ok=true;web_resume_wrong_account=true;resume_checks=0;
        assert(ResumeLogin(game,saved,cancelled,progress,&refreshed)==ResumeStatus::Rejected);
        assert(web_resumes==3 && resume_checks==1 && exchanges==before && !refreshed);
        web_resume_wrong_account=false;
    }
    resume_scenario.reset();
    // Post-dispatch game login is a separate grant, not another gamerLogin or
    // allocation. Same-account checks are mandatory and only hkrpg opts in.
    for(const char* id:{"hk4e","hkrpg"}) {
        expected_profile=FindGameProfile(id);assert(expected_profile);
        const auto& game=*expected_profile;
        login_device=std::string(32,'a');
        const Authorization saved{{std::to_string(game.app_id),"1","123","cloud-combo-token"},
            NativeWebIdentity(game,login_device),"123","saved-account-token"};
        const auto exercise=[&](ResumeScenario value,bool expect_refresh,bool do_cancel=false) {
            resume_scenario=value;cancelled=do_cancel;events.clear();
            const auto before_exchanges=exchanges,before_logins=cloud_logins,before_fetches=qr_fetches,
                before_queries=queries,before_dispatches=dispatched;
            const auto refreshed=RefreshGameSdkLogin(game,saved,cancelled,progress);
            assert(refreshed.has_value()==expect_refresh);
            assert(cloud_logins==before_logins && qr_fetches==before_fetches && queries==before_queries &&
                   dispatched==before_dispatches);
            assert(exchanges-before_exchanges==(game.id=="hkrpg" && !do_cancel?1u:0u));
            assert(saved.combo.combo_token=="cloud-combo-token" && saved.channel_token=="saved-account-token");
            if(refreshed) {
                assert(refreshed->app_id==saved.combo.app_id && refreshed->open_id==saved.combo.open_id &&
                    refreshed->combo_token=="refreshed-combo-token");
                assert(std::find(events.begin(),events.end(),"GAME_SDK_LOGIN_REFRESHED")!=events.end());
            } else assert(std::find(events.begin(),events.end(),"GAME_SDK_LOGIN_REFRESHED")==events.end());
        };
        exercise(ResumeScenario::Success,game.id=="hkrpg");
        exercise(ResumeScenario::Success,false,true);
        if(game.id!="hkrpg") continue;
        for(auto value:{ResumeScenario::ExchangeRejected,ResumeScenario::Risk,ResumeScenario::Verification,
                       ResumeScenario::Network,ResumeScenario::Http,ResumeScenario::Malformed,
                       ResumeScenario::WrongAccount,ResumeScenario::WrongGame,ResumeScenario::WrongChannel,
                       ResumeScenario::WrongOpenId,ResumeScenario::CancelExchange}) exercise(value,false);
        for(const auto& token:{std::string{},std::string("bad\ntoken")}) {
            auto unavailable=saved;unavailable.channel_token=token;cancelled=false;
            const auto before=exchanges;
            assert(!RefreshGameSdkLogin(game,unavailable,cancelled,progress) && exchanges==before);
        }
    }
    resume_scenario.reset();
    std::filesystem::remove_all(directory);
}
