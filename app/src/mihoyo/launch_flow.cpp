#include "mihoyo/launch_flow.hpp"
#include "mihoyo/launcher_status.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/stream_startup_policy.hpp"
#include "mihoyo/dispatch_prepare.hpp"
#include "mihoyo/initial_dispatch.hpp"
#include "mihoyo/queue_ticket.hpp"
#include "mihoyo/combo_exchange.hpp"
#include "mihoyo/ping_probe.hpp"
#include "mihoyo/passport_token_resume.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <mbedtls/base64.h>
#include <algorithm>
#include <chrono>
#include <random>
#include <thread>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin {
namespace {
using Clock = std::chrono::steady_clock;
bool Wait(const std::atomic_bool& cancelled, std::chrono::milliseconds delay) {
    const auto until = Clock::now()+delay;
    while (!cancelled && Clock::now() < until) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    return !cancelled;
}
std::string DeviceId() {
    unsigned char bytes[16];
#ifdef __SWITCH__
    randomGet(bytes,sizeof(bytes));
#else
    std::random_device random;
    for (auto& b : bytes) b = static_cast<unsigned char>(random());
#endif
    std::string value;
    constexpr char digits[]="0123456789abcdef";
    for (auto b : bytes) { value += digits[b>>4]; value += digits[b&15]; }
    return value;
}
std::string Base64(const std::string& text) {
    std::string out(4*((text.size()+2)/3)+1,'\0');
    std::size_t count = 0;
    if (mbedtls_base64_encode(reinterpret_cast<unsigned char*>(out.data()),out.size(),&count,
        reinterpret_cast<const unsigned char*>(text.data()),text.size()) != 0) return {};
    out.resize(count); return out;
}
const char* DispatchEvent(dispatch::DispatchStatus status) {
    using S=dispatch::DispatchStatus;
    switch(status) {
        case S::Maintenance: return "GAME_MAINTENANCE";
        case S::BadNetwork: return "DISPATCH_BAD_NETWORK";
        case S::Unavailable: case S::SwitchUnavailable: return "NO_AVAILABLE_NODE";
        case S::DeviceConflict: return "ACCOUNT_DEVICE_CONFLICT";
        case S::KickedOut: return "QUEUE_KICKED_OUT";
        case S::TimedOut: return "QUEUE_EXPIRED";
        case S::ApiRejected: return "DISPATCH_REJECTED";
        case S::HttpError: return "DISPATCH_HTTP_ERROR";
        case S::TransportError: return "DISPATCH_NETWORK_ERROR";
        default: return "DISPATCH_INVALID_RESPONSE";
    }
}
const char* QrFailureEvent(auth::QrStatus status) {
    using S=auth::QrStatus;
    switch(status) {
        case S::RiskRejected: return "QR_OFFICIAL_RISK_REJECTED";
        case S::VerificationRequired: return "QR_OFFICIAL_VERIFICATION_REQUIRED";
        case S::Expired: return "QR_EXPIRED_NEW_QR_REQUIRED";
        case S::Cancelled: return "QR_PHONE_CANCELLED";
        case S::UnsupportedProtocol: return "QR_CONFIRM_PROTOCOL_UNSUPPORTED";
        case S::Rejected: return "QR_QUERY_API_REJECTED";
        case S::HttpError: return "QR_QUERY_HTTP_FAILED";
        case S::TransportError: return "QR_QUERY_NETWORK_FAILED";
        default: return "QR_QUERY_RESPONSE_INVALID";
    }
}
} // namespace

ResumeStatus ResumeLogin(const GameProfile& profile, const Authorization& authorization,
                         const std::atomic_bool& cancelled, const Progress& progress,
                         std::optional<Authorization>* refreshed) {
    if(cancelled) return ResumeStatus::Cancelled;
    const auto request=auth::BuildGamerLoginRequest(profile,authorization.combo,authorization.device);
    if(!request || authorization.account_id.empty()) {
        progress("SAVED_LOGIN_REJECTED",0,0);return ResumeStatus::Rejected;
    }
    progress("SAVED_LOGIN_CHECKING",0,0);
    auto result=auth::CheckGamerLogin(profile,*request,&cancelled);
    if(cancelled) return ResumeStatus::Cancelled;
    // The official H5 TOKEN_INVALID code is -100. A stored account token can
    // outlive its per-game Combo session: use the same verified exchange as
    // Account QR login, at most once, before asking for another QR. Never try
    // this for entitlement/risk errors, transport failure or without a caller
    // that can retain the newly verified authorization.
    const bool token_expired=result.status==auth::GamerLoginStatus::Rejected && result.retcode==-100;
    if(token_expired && refreshed && profile.id=="hkrpg" && !authorization.passport_mid.empty()) {
        // A web grant must stay a web grant. Do not downgrade it through the
        // legacy Combo-QR exchange and lose the official Passport context.
        progress("SAVED_LOGIN_REFRESHING",0,0);
        auto candidate=ResumePassportFromAccount(profile,authorization,cancelled,progress);
        if(cancelled) return ResumeStatus::Cancelled;
        if(!candidate) {progress("SAVED_LOGIN_RETRY_LATER",0,0);return ResumeStatus::RetryLater;}
        if(candidate->account_id!=authorization.account_id || candidate->combo.open_id!=authorization.combo.open_id ||
           candidate->combo.app_id!=authorization.combo.app_id || candidate->combo.channel_id!=authorization.combo.channel_id) {
            progress("SAVED_LOGIN_REFRESH_ACCOUNT_MISMATCH",0,0);return ResumeStatus::Rejected;
        }
        const auto verified=auth::BuildGamerLoginRequest(profile,candidate->combo,candidate->device);
        if(!verified) {progress("SAVED_LOGIN_RETRY_LATER",0,0);return ResumeStatus::RetryLater;}
        result=auth::CheckGamerLogin(profile,*verified,&cancelled);
        if(cancelled) return ResumeStatus::Cancelled;
        if(result.status==auth::GamerLoginStatus::Success) {
            *refreshed=std::move(candidate);progress("SAVED_LOGIN_REFRESHED",0,result.http_status);
        }
    } else if(token_expired &&
       refreshed && !authorization.channel_token.empty()) {
        const auto exchange=auth::BuildComboExchangeRequest(profile,
            {authorization.account_id,authorization.channel_token},authorization.device.device_id);
        if(exchange) {
            progress("SAVED_LOGIN_REFRESHING",0,0);
            const auto exchanged=auth::ExchangeAccountForCombo(profile,*exchange,&cancelled);
            if(cancelled) return ResumeStatus::Cancelled;
            if(exchanged.status!=auth::QrStatus::Confirmed || !exchanged.authorization) {
                if(exchanged.status==auth::QrStatus::Rejected || exchanged.status==auth::QrStatus::Expired ||
                   exchanged.status==auth::QrStatus::RiskRejected || exchanged.status==auth::QrStatus::VerificationRequired) {
                    progress("SAVED_LOGIN_REFRESH_REJECTED",exchanged.retcode,exchanged.http_status);
                    progress("SAVED_LOGIN_REJECTED",exchanged.retcode,exchanged.http_status);
                    return ResumeStatus::Rejected;
                }
                progress("SAVED_LOGIN_RETRY_LATER",exchanged.retcode,exchanged.http_status);
                return ResumeStatus::RetryLater;
            }
            const auto& combo=*exchanged.authorization;
            if(combo.app_id!=AppIdText(profile) || combo.channel_id!=authorization.combo.channel_id ||
               combo.open_id!=authorization.combo.open_id ||
               (!exchanged.account_id.empty() && exchanged.account_id!=authorization.account_id)) {
                progress("SAVED_LOGIN_REFRESH_ACCOUNT_MISMATCH",0,0);
                progress("SAVED_LOGIN_REJECTED",0,0);
                return ResumeStatus::Rejected;
            }
            Authorization candidate=authorization;
            candidate.combo=combo;
            const auto candidate_request=auth::BuildGamerLoginRequest(profile,candidate.combo,candidate.device);
            if(!candidate_request) {
                progress("SAVED_LOGIN_RETRY_LATER",0,0);return ResumeStatus::RetryLater;
            }
            result=auth::CheckGamerLogin(profile,*candidate_request,&cancelled);
            if(cancelled) return ResumeStatus::Cancelled;
            if(result.status==auth::GamerLoginStatus::Success) {
                *refreshed=std::move(candidate);
                progress("SAVED_LOGIN_REFRESHED",0,result.http_status);
            }
        }
    }
    if(result.status==auth::GamerLoginStatus::Success) {
        progress("LOGIN_RESTORED",0,result.http_status);return ResumeStatus::Valid;
    }
    if(result.status==auth::GamerLoginStatus::Rejected) {
        progress("SAVED_LOGIN_REJECTED",result.retcode,result.http_status);return ResumeStatus::Rejected;
    }
    if(result.status==auth::GamerLoginStatus::TransportError)
        progress("SAVED_LOGIN_NETWORK_ERROR",result.transport_error,result.http_status);
    progress("SAVED_LOGIN_RETRY_LATER",result.retcode,result.http_status);
    return ResumeStatus::RetryLater;
}

std::optional<auth::ComboLoginFields> RefreshGameSdkLogin(const GameProfile& profile,
    const Authorization& authorization, const std::atomic_bool& cancelled, const Progress& progress) {
    if(cancelled || profile.id!="hkrpg") return std::nullopt;
    // The H5 SDK requests a fresh Combo during login_login, not the earlier
    // cloud gamer-login result. Without a retained H5 cookie session use the
    // already verified native account -> Combo endpoint. This is NOT webLogin
    // and is not evidence that both dialects are interchangeable.
    const auto request=auth::BuildComboExchangeRequest(profile,
        {authorization.account_id,authorization.channel_token},authorization.device.device_id);
    if(!request || authorization.combo.app_id!=AppIdText(profile) ||
       authorization.combo.channel_id!="1" || authorization.combo.open_id.empty()) {
        progress("GAME_SDK_LOGIN_REFRESH_UNAVAILABLE",0,0);return std::nullopt;
    }
    progress("GAME_SDK_LOGIN_REFRESHING",0,0);
    const auto result=auth::ExchangeAccountForCombo(profile,*request,&cancelled);
    if(cancelled) return std::nullopt;
    if(result.status!=auth::QrStatus::Confirmed || !result.authorization) {
        progress("GAME_SDK_LOGIN_REFRESH_FAILED",result.retcode,result.http_status);
        return std::nullopt;
    }
    const auto& combo=*result.authorization;
    if(combo.app_id!=authorization.combo.app_id || combo.channel_id!=authorization.combo.channel_id ||
       combo.open_id!=authorization.combo.open_id ||
       (!result.account_id.empty() && result.account_id!=authorization.account_id)) {
        progress("GAME_SDK_LOGIN_REFRESH_ACCOUNT_MISMATCH",0,0);return std::nullopt;
    }
    // Reuse field validation only; do not submit another cloud gamerLogin
    // which would change the authorization of the already allocated session.
    if(!auth::BuildGamerLoginRequest(profile,combo,authorization.device)) {
        progress("GAME_SDK_LOGIN_REFRESH_FAILED",0,result.http_status);return std::nullopt;
    }
    progress("GAME_SDK_LOGIN_REFRESHED",1,result.http_status);
    progress("GAME_SDK_LOGIN_H5_ADDITIONAL_ROOT_FIELDS",static_cast<int>(result.sdk_h5_additional_root_fields),0);
    return combo;
}

std::optional<Authorization> Login(const GameProfile& profile, const std::atomic_bool& cancelled,
    const std::function<void(const std::string&)>& render_qr, const Progress& progress) {
    if (cancelled) return std::nullopt;
    Authorization authorization;
    authorization.device = NativeWebIdentity(profile, DeviceId());
    progress("QR_FETCH",0,0);
    auto fetched = auth::FetchQr(profile,authorization.device.device_id,&cancelled);
    if (cancelled) return std::nullopt;
    if (fetched.status != auth::QrStatus::Ready || !fetched.challenge) {
        progress(fetched.verification_header_present ? "QR_AIGIS_HEADER_PRESENT" : "QR_AIGIS_HEADER_ABSENT",0,0);
        progress("QR_FETCH_FAILED",fetched.retcode,fetched.http_status); return std::nullopt;
    }
    render_qr(fetched.challenge->url);
    progress("QR_WAITING",0,fetched.http_status);
    const auto expires = Clock::now()+std::chrono::minutes(3);
    bool scanned = false;
    unsigned transient_errors = 0;
    unsigned polls = 0;
    while (Wait(cancelled,std::chrono::seconds(2)) && Clock::now()<expires) {
        auto queried = auth::QueryQr(profile,authorization.device.device_id,fetched.challenge->ticket,&cancelled);
        ++polls;
        if (cancelled) break;
        if (queried.status == auth::QrStatus::Waiting) { transient_errors=0; continue; }
        if (queried.status == auth::QrStatus::Scanned) {
            transient_errors=0;
            if (!scanned) { scanned=true; progress("QR_SCANNED_CONFIRM_ON_PHONE",0,queried.http_status); }
            continue;
        }
        if (queried.status == auth::QrStatus::Confirmed &&
            (queried.authorization || queried.account_authorization)) {
            // Dispose of QR material before doing authenticated requests.
            render_qr(""); fetched.challenge.reset();
            if (queried.account_authorization) {
                progress("QR_ACCOUNT_CONFIRMED_COMBO_EXCHANGE",0,queried.http_status);
                auto account=std::move(*queried.account_authorization);
                const auto exchange=auth::BuildComboExchangeRequest(profile,account,authorization.device.device_id);
                if (!exchange) {progress("COMBO_EXCHANGE_INVALID_FIELDS",0,0);return std::nullopt;}
                queried=auth::ExchangeAccountForCombo(profile,*exchange,&cancelled);
                if (cancelled) return std::nullopt;
                if (queried.status!=auth::QrStatus::Confirmed || !queried.authorization) {
                    progress(queried.verification_header_present ? "QR_AIGIS_HEADER_PRESENT" : "QR_AIGIS_HEADER_ABSENT",0,0);
                    progress("COMBO_EXCHANGE_FAILED",queried.retcode,queried.http_status);return std::nullopt;
                }
                // The official verifier may return account_type data without
                // repeating uid. The scanned passport uid is the account aid.
                queried.account_id=std::move(account.uid);
                // The official page reports the account session's stoken to the
                // in-game SDK as channel_token.
                authorization.channel_token=std::move(account.token);
            } else progress("QR_COMBO_CONFIRMED",0,queried.http_status);
            authorization.combo = std::move(*queried.authorization);
            authorization.account_id = std::move(queried.account_id);
            progress("CLOUD_ACCOUNT_AUTH",0,0);
            const auto request = auth::BuildGamerLoginRequest(profile,authorization.combo,authorization.device);
            if (!request) { progress("CLOUD_AUTH_INVALID_FIELDS",0,0); return std::nullopt; }
            const auto login = auth::CheckGamerLogin(profile,*request,&cancelled);
            if (cancelled) return std::nullopt;
            if (login.status != auth::GamerLoginStatus::Success) {
                if(login.status==auth::GamerLoginStatus::TransportError)
                    progress("CLOUD_AUTH_NETWORK_ERROR",login.transport_error,login.http_status);
                // -100002 is the cloud service gate seen on nap; keep the
                // official request unchanged and surface a clear account
                // eligibility message instead of a generic network failure.
                progress(login.retcode == -100002 ? "CLOUD_AUTH_SERVICE_UNAVAILABLE"
                                                  : "CLOUD_AUTH_FAILED",
                         login.retcode,login.http_status);
                return std::nullopt;
            }
            progress(authorization.account_id.empty() ? "LOGIN_OK_ACCOUNT_ID_MISSING" : "LOGIN_OK",0,login.http_status);
            return authorization;
        }
        if (queried.status == auth::QrStatus::TransportError || queried.status == auth::QrStatus::HttpError) {
            if (++transient_errors < 3) continue;
        }
        render_qr("");
        progress(queried.verification_header_present ? "QR_AIGIS_HEADER_PRESENT" : "QR_AIGIS_HEADER_ABSENT",0,0);
        const auto elapsed=std::chrono::duration_cast<std::chrono::seconds>(Clock::now()-(expires-std::chrono::minutes(3))).count();
        progress("QR_QUERY_POLL_COUNT",static_cast<int>(polls),0);
        progress("QR_QUERY_ELAPSED_SECONDS",static_cast<int>(elapsed),0);
        progress(QrFailureEvent(queried.status),queried.retcode,queried.http_status); return std::nullopt;
    }
    render_qr("");
    progress(cancelled ? "LOGIN_CANCELLED" : "QR_EXPIRED",0,0); return std::nullopt;
}

std::optional<dispatch::SdkStartGameParams> Launch(const GameProfile& profile,
    const Authorization& auth, const std::atomic_bool& cancelled, const Progress& progress,LaunchOptions options) {
    using namespace dispatch;
    if (cancelled) return std::nullopt;
    if (auth.account_id.empty()) { progress("ACCOUNT_ID_REQUIRED_NO_DISPATCH",0,0); return std::nullopt; }
    if(options.wait_in_long_normal_queue) progress("AUTOTEST_LONG_QUEUE_WAIT_ENABLED",1800,0);
    const auto report_transport = [&](const opennow::HttpTransferDiagnostics& d, long status) {
        if (!d.captured) return;
        progress("HTTP_FAILURE_PHASE",static_cast<int>(d.phase(status)),status);
        progress("HTTP_FAILURE_DNS_MS",d.lookup_ms,0);
        progress("HTTP_FAILURE_CONNECT_MS",d.connect_ms,0);
        progress("HTTP_FAILURE_TLS_MS",d.tls_ms,0);
        progress("HTTP_FAILURE_READY_MS",d.ready_ms,0);
        progress("HTTP_FAILURE_FIRST_BYTE_MS",d.first_byte_ms,0);
        progress("HTTP_FAILURE_TOTAL_MS",d.total_ms,0);
        progress("HTTP_FAILURE_UPLOADED_BYTES",d.uploaded_bytes,0);
    };
    std::string measured_ping_node,ping_sdk_param;
    const auto prepare = [&](PrepareOperation operation, const Node& node={}) {
        const auto request = BuildPrepareRequest(profile,operation,auth.combo,auth.device,node,measured_ping_node);
        const auto result=request ? SubmitPrepare(profile,*request,&cancelled) : PrepareResult{};
        if(!cancelled && result.status==PrepareStatus::TransportError) {
            progress("DISPATCH_PREPARE_NETWORK_OPERATION",static_cast<int>(operation)+1,0);
            progress("DISPATCH_PREPARE_NETWORK_ERROR",result.transport_error,result.http_status);
            report_transport(result.transport_diagnostics,result.http_status);
        }
        return result;
    };
    for (const auto operation : {PrepareOperation::Status,PrepareOperation::PingServers}) {
        progress(operation == PrepareOperation::Status ? "CHECK_SERVICE" : "GET_PING_SERVERS",0,0);
        const auto result = prepare(operation);
        if (cancelled) return std::nullopt;
        if (result.status != PrepareStatus::Success) {
            progress(result.status == PrepareStatus::Maintenance ? "GAME_MAINTENANCE" : "SERVICE_CHECK_FAILED",
                     result.retcode,result.http_status); return std::nullopt;
        }
        if(operation==PrepareOperation::PingServers) ping_sdk_param=result.ping_sdk_param;
    }
    const auto ping_servers=DecodePingServers(ping_sdk_param);
    if(!ping_servers || ping_servers->empty()) {progress("PING_SDK_METADATA_INVALID",0,0);return std::nullopt;}
    progress("PING_SERVER_COUNT",static_cast<int>(ping_servers->size()),0);
    progress("PING_WSS_MEASURING",0,0);
    const auto measured=ProbePingServers(*ping_servers,cancelled);
    if(cancelled) {progress("PING_CANCELLED",0,0);return std::nullopt;}
    progress("PING_TARGETS_TESTED",static_cast<int>(measured.tested),0);
    progress("PING_TARGETS_SUCCEEDED",static_cast<int>(measured.succeeded),0);
    if(measured.succeeded<measured.tested) {
        progress("PING_INVALID_ENDPOINT_COUNT",static_cast<int>(measured.invalid_endpoints),0);
        progress("PING_CONNECT_FAILED_COUNT",static_cast<int>(measured.connect_failures),0);
        progress("PING_CONNECT_TIMEOUT_COUNT",static_cast<int>(measured.connect_timeouts),0);
        progress("PING_ECHO_FAILED_COUNT",static_cast<int>(measured.echo_failures),0);
        progress("PING_FIRST_CONNECTION_ERROR",measured.first_curl_code,measured.first_http_status);
        progress("PING_FIRST_TRANSPORT_STAGE",measured.first_transport_stage,0);
        progress("PING_FIRST_ELAPSED_MS",measured.first_elapsed_ms,0);
        progress("PING_FIRST_PORT",measured.first_port,0);
        progress("PING_FIRST_OS_ERROR",measured.first_os_error,0);
        if(measured.first_transport.captured) {
            const auto& d=measured.first_transport;
            progress("PING_FIRST_FAILURE_PHASE",static_cast<int>(d.phase(measured.first_http_status)),0);
            progress("PING_FIRST_DNS_MS",d.lookup_ms,0);
            progress("PING_FIRST_CONNECT_MS",d.connect_ms,0);
            progress("PING_FIRST_TLS_MS",d.tls_ms,0);
            progress("PING_FIRST_READY_MS",d.ready_ms,0);
            progress("PING_FIRST_TOTAL_MS",d.total_ms,0);
        }
    }
    if(measured.node_json.empty()) {progress("PING_ALL_FAILED_NO_NODE_REQUEST",0,0);return std::nullopt;}
    measured_ping_node=measured.node_json;
    progress("GET_NODES_WITH_MEASURED_RTT",0,0);
    const auto nodes = prepare(PrepareOperation::Nodes);
    if (cancelled) return std::nullopt;
    if (nodes.status != PrepareStatus::Success || nodes.nodes.empty()) {
        progress("GET_NODES_FAILED",nodes.retcode,nodes.http_status); return std::nullopt;
    }
    progress("NODE_CANDIDATES_READY",static_cast<int>(nodes.nodes.size()),nodes.http_status);
    auto selected = std::find_if(nodes.nodes.begin(),nodes.nodes.end(),[](const Node& n){return n.recommended;});
    if (selected == nodes.nodes.end()) selected=nodes.nodes.begin();
    progress("VERIFY_NODE",0,0);
    const auto verified = prepare(PrepareOperation::Verify,*selected);
    if (cancelled) return std::nullopt;
    if (verified.status != PrepareStatus::Success) {
        progress("NODE_VERIFY_FAILED",verified.retcode,verified.http_status); return std::nullopt;
    }
    const auto device_info=nlohmann::json{{"deviceName","Nintendo Switch"},{"systemVersion","Horizon"},
        {"deviceInfo","operatingSystem:Horizon&deviceModel:Switch&processorCount:4&processorType:ARM64&graphicsDeviceName:Tegra"}}.dump();
    InitialDispatchParams params;
    params.bit_rate=kStartupBitRate; params.fps=kStartupStreamFps; params.node_id=selected->id;
    params.regions_json=selected->regions_json; params.resolution="1280x720"; params.environment="2";
    progress("DISPATCH_BIT_RATE",static_cast<int>(params.bit_rate),0);
    auto user_data=nlohmann::json{{"dpi",96},{"w",1280},{"h",720},{"lang","zh-cn"},{"di",Base64(device_info)}};
    // Official 7.1.0 J5.a.k(): using_new_cmd_line carries the launch-time
    // default-pad preference as user_data.idg (the legacy spelling was
    // -is_default_gamepad 1). It belongs to signed dispatch, not StartGameReq.
    // All CN cloud titles in this table are launched by the same web client
    // family and accept the same signed hint; it was only ever wired for hk4e,
    // which left the pod's virtual pad unrequested on the other two titles.
    if(profile.id=="hk4e" || profile.id=="hkrpg") {
        user_data["idg"]=true;
        progress("DISPATCH_DEFAULT_GAMEPAD_REQUESTED",1,0);
    }
    params.user_data_json=user_data.dump();
    // Normal queue only. Never silently switch to the paid/coin queue.
    const auto request=BuildInitialDispatchRequest(profile,auth.combo,auth.device,params);
    if (!request) {progress("DISPATCH_INVALID_FIELDS",0,0);return std::nullopt;}
    progress("DISPATCH_NORMAL_QUEUE",0,0);
    auto dispatched=SubmitInitialDispatch(profile,*request,&cancelled);
    std::string ticket=dispatched.ticket;
    // Cleanup must run on every exit, including cancellation and ACK failure.
    struct QueueCleanup {
        const GameProfile& profile; const Authorization& auth; std::string& ticket; const Progress& progress;
        ~QueueCleanup() {
            if (ticket.empty()) return;
            const auto leave=BuildQueueRequest(profile,QueueOperation::Leave,auth.combo,auth.device,ticket);
            const auto result=leave ? SubmitQueueAction(profile,*leave,nullptr,5000) : QueueActionResult{};
            progress(result.status==QueueActionStatus::Success ? "QUEUE_LEFT" : "QUEUE_LEAVE_FAILED",result.retcode,result.http_status);
        }
    } cleanup{profile,auth,ticket,progress};
    const auto queue_deadline=Clock::now()+QueueWaitLimit(options);
    // Preserve the user's default long-queue exit policy. The launcher keeps
    // this reason visible and opens an explanatory notice after cleanup.
    while (dispatched.status==DispatchStatus::Queued && !cancelled && Clock::now()<queue_deadline) {
        // Android DispatchQueueInfo / EnqueueService displays queue_rank.
        // branch_queue_len is a size metric, not our position. A busy queue
        // must not eject a nearly-ready ticket because its total size grew.
        progress("QUEUE_TOTAL_LENGTH",dispatched.queue_length.value_or(-1),dispatched.http_status);
        progress("QUEUE_BRANCH_LENGTH",dispatched.branch_queue_length.value_or(-1),dispatched.http_status);
        const int position=dispatched.queue_rank.value_or(-1);
        progress("QUEUED_POSITION",position,dispatched.http_status);
        if(!options.wait_in_long_normal_queue && launcher::LeavesLongQueue(position)) {
            progress("QUEUE_ABANDONED_TOO_LONG",position,dispatched.http_status);
            return std::nullopt;
        }
        if (!Wait(cancelled,std::chrono::seconds(dispatched.query_interval_seconds))) break;
        const auto poll=BuildQueueRequest(profile,QueueOperation::Poll,auth.combo,auth.device,ticket);
        dispatched=poll ? PollQueue(profile,*poll,&cancelled) : DispatchResponse{};
        // Only the read-only ticket query is retryable. Never replay initial
        // allocation or ACK: a lost response must not allocate a second pod.
        for(unsigned retry=0;poll && !cancelled && Clock::now()<queue_deadline &&
            dispatched.status==DispatchStatus::TransportError &&
            dispatched.retryable_transport && retry<2;++retry) {
            progress("QUEUE_POLL_NETWORK_RETRY",dispatched.transport_error,dispatched.http_status);
            if(!Wait(cancelled,std::chrono::milliseconds(500*(retry+1)))) break;
            dispatched=PollQueue(profile,*poll,&cancelled);
        }
    }
    if (cancelled) {progress("LAUNCH_CANCELLED",0,0);return std::nullopt;}
    if (dispatched.status==DispatchStatus::Queued) {
        progress("QUEUE_EXPIRED",0,dispatched.http_status);return std::nullopt;
    }
    if (dispatched.status!=DispatchStatus::Finished) {
        if (dispatched.status==DispatchStatus::TransportError)
            report_transport(dispatched.transport_diagnostics,dispatched.http_status);
        progress(DispatchEvent(dispatched.status),dispatched.status==DispatchStatus::TransportError ?
            dispatched.transport_error : dispatched.retcode,dispatched.http_status);return std::nullopt;
    }
    if (!dispatched.sdk_start_game_params || dispatched.sdk_start_game_params->game_token.empty() ||
        dispatched.sdk_start_game_params->sid.empty() || dispatched.sdk_start_game_params->pod_id.empty() ||
        !dispatched.sdk_start_game_params->game_server_wss_url.starts_with("wss://")) {
        progress("DISPATCH_MISSING_REQUIRED_START_FIELDS",0,0);return std::nullopt;
    }
    if (dispatched.requires_ticket_ack) {
        const auto ack=BuildQueueRequest(profile,QueueOperation::Acknowledge,auth.combo,auth.device,ticket);
        const auto result=ack ? SubmitQueueAction(profile,*ack,&cancelled) : QueueActionResult{};
        if (result.status!=QueueActionStatus::Success || cancelled) {
            progress("QUEUE_ACK_FAILED",result.retcode,result.http_status);return std::nullopt;
        }
    }
    ticket.clear(); // acknowledged; Session now owns stopping the allocated game
    progress("DISPATCH_READY_FOR_GAME_AUTH",0,dispatched.http_status);
    return std::move(dispatched.sdk_start_game_params);
}
} // namespace cloudgenshin
