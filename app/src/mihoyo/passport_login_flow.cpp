#include "mihoyo/passport_login_flow.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/passport_signin_policy.hpp"

namespace cloudgenshin {
namespace {
void Failure(const auth::PassportReply& reply,const Progress& progress) {
    using auth::QrStatus;
    const char* event="PASSPORT_RESPONSE_INVALID";
    switch(reply.status) {
    case QrStatus::Expired:event="QR_EXPIRED";break;
    case QrStatus::Cancelled:event="QR_CANCELLED";break;
    case QrStatus::RiskRejected:event="QR_OFFICIAL_RISK_REJECTED";break;
    case QrStatus::VerificationRequired:event="PASSPORT_VERIFICATION_REQUIRED";break;
    case QrStatus::TransportError:case QrStatus::HttpError:event="PASSPORT_NETWORK_FAILED";break;
    case QrStatus::Rejected:event="PASSPORT_API_REJECTED";break;
    default:break;
    }
    progress(event,reply.retcode,reply.http_status);
}
}
std::optional<Authorization> RunPassportLogin(const GameProfile& profile,
    const auth::PassportIdentity& identity,const std::atomic_bool& cancelled,
    const std::function<void(const std::string&)>& render_qr,const Progress& progress,
    const PassportLoginTransport& transport) {
    using auth::PassportStep; using auth::QrStatus;
    if(cancelled) return std::nullopt;
    if(!auth::SupportsPassportLogin(profile) || !auth::BuildPassportHeaders(profile,identity,false) ||
       !transport.request || !transport.wait) {
        progress("PASSPORT_PROFILE_UNSUPPORTED",0,0);return std::nullopt;
    }
    progress("PASSPORT_LOGIN_START",0,0);
    progress("QR_FETCH",0,0);
    auto created=transport.request(PassportStep::Create,{});
    if(cancelled) return std::nullopt;
    if(created.status!=QrStatus::Ready || !created.challenge) {Failure(created,progress);return std::nullopt;}
    render_qr(created.challenge->url);
    struct ClearQr {
        const std::function<void(const std::string&)>& render;
        ~ClearQr(){render("");}
    } clear{render_qr};
    progress("QR_WAITING",0,created.http_status);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::minutes(3);
    bool scanned=false,confirmed=false;
    unsigned transient_failures=0;
    for(unsigned polls=0;polls<90 && !cancelled && std::chrono::steady_clock::now()<deadline;++polls) {
        if(!transport.wait(std::chrono::seconds(2)) || cancelled) return std::nullopt;
        const auto queried=transport.request(PassportStep::Query,created.challenge->ticket);
        if(cancelled) return std::nullopt;
        if(queried.status==QrStatus::Waiting || queried.status==QrStatus::Scanned) {
            transient_failures=0;
            if(queried.status==QrStatus::Scanned && !scanned) {
                scanned=true;progress("QR_SCANNED_CONFIRM_ON_PHONE",0,queried.http_status);
            }
            continue;
        }
        if(queried.status==QrStatus::Confirmed) {confirmed=true;break;}
        if((queried.status==QrStatus::TransportError || queried.status==QrStatus::HttpError) &&
            ++transient_failures<3) continue;
        Failure(queried,progress);return std::nullopt;
    }
    render_qr("");created.challenge.reset();
    if(cancelled) return std::nullopt;
    if(!confirmed) {progress("QR_EXPIRED",0,0);return std::nullopt;}
    progress("PASSPORT_QR_CONFIRMED",0,200);
    progress("PASSPORT_WEB_VERIFY",0,0);
    const auto verified=transport.request(PassportStep::Verify,{});
    if(cancelled) return std::nullopt;
    if(verified.status!=QrStatus::Confirmed || verified.channel_token.empty() || verified.account_id.empty()) {
        Failure(verified,progress);return std::nullopt;
    }
    if(auth::UsesPassportQr(profile.id) && !auth::ValidPassportMid(verified.passport_mid)) {
        progress("PASSPORT_SESSION_CONTEXT_MISSING",0,verified.http_status);return std::nullopt;
    }
    progress("PASSPORT_STOKEN_VERIFIED",1,verified.http_status);
    progress("PASSPORT_COMBO_WEB_LOGIN",0,0);
    auto combo=transport.request(PassportStep::Combo,{});
    if(cancelled) return std::nullopt;
    if(combo.status!=QrStatus::Confirmed || !combo.combo) {Failure(combo,progress);return std::nullopt;}
    if(combo.account_id!=verified.account_id) {
        progress("PASSPORT_ACCOUNT_MISMATCH",0,0);return std::nullopt;
    }
    Authorization authorization{std::move(*combo.combo),NativeWebIdentity(profile,identity.device_id),
        verified.account_id,verified.channel_token,verified.passport_mid};
    const auto request=auth::BuildGamerLoginRequest(profile,authorization.combo,authorization.device);
    if(!request) {progress("CLOUD_AUTH_INVALID_FIELDS",0,0);return std::nullopt;}
    progress("CLOUD_ACCOUNT_AUTH",0,0);
    const auto login=auth::CheckGamerLogin(profile,*request,&cancelled);
    if(cancelled) return std::nullopt;
    if(login.status!=auth::GamerLoginStatus::Success) {
        progress("CLOUD_AUTH_FAILED",login.retcode,login.http_status);return std::nullopt;
    }
    progress("LOGIN_CHANNEL_TOKEN_PRESENT",1,0);
    progress("LOGIN_OK",0,login.http_status);
    return authorization;
}
} // namespace cloudgenshin
