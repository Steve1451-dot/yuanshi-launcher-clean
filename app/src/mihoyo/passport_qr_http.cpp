#include "mihoyo/passport_login_flow.hpp"
#include "mihoyo/production_profile.hpp"
#include "http_client.hpp"
#include "app_version.hpp"
#include <array>
#include <random>
#include <thread>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::auth {
struct PassportSession::Impl {
    const GameProfile& profile;
    PassportIdentity identity;
    const std::atomic_bool& cancelled;
    CURLSH* cookies=nullptr;
    Impl(const GameProfile& game,PassportIdentity device,const std::atomic_bool& stop)
        :profile(game),identity(std::move(device)),cancelled(stop) {
        cookies=curl_share_init();
        if(cookies && curl_share_setopt(cookies,CURLSHOPT_SHARE,CURL_LOCK_DATA_COOKIE)!=CURLSHE_OK) {
            curl_share_cleanup(cookies);cookies=nullptr;
        }
    }
    ~Impl(){if(cookies) curl_share_cleanup(cookies);}
};
PassportSession::PassportSession(const GameProfile& profile,PassportIdentity identity,
                                 const std::atomic_bool& cancelled)
    :impl_(std::make_unique<Impl>(profile,std::move(identity),cancelled)) {}
PassportSession::~PassportSession()=default;
PassportReply PassportSession::request(PassportStep step,const std::string& ticket) {
    PassportReply failed;
    if(impl_->cancelled){failed.status=QrStatus::Cancelled;return failed;}
    const auto headers=BuildPassportHeaders(impl_->profile,impl_->identity,step==PassportStep::Combo);
    const auto body=BuildPassportBody(step,impl_->profile,ticket);
    if(!headers || !body || !impl_->cookies) return failed;
    // No server-supplied URL is followed. Every credential-bearing request is
    // fixed to this official passport/game profile, with TLS checks intact.
    std::string url;
    switch(step) {
    case PassportStep::Create:url="https://passport-api.mihoyo.com/account/ma-cn-passport/web/createQRLogin";break;
    case PassportStep::Query:url="https://passport-api.mihoyo.com/account/ma-cn-passport/web/queryQRLoginStatus";break;
    case PassportStep::Verify:url="https://passport-api.mihoyo.com/account/ma-cn-session/web/webVerifyForGame";break;
    case PassportStep::Combo:url=std::string(impl_->profile.sdk_base)+"/combo/granter/login/webLogin";break;
    }
    try {
        const auto reply=opennow::HttpClient{}.Post(url,
            std::string("CloudGenshin-NX/")+opennow::kCloudGenshinVersion,*headers,*body,{},
            {.cancelled=&impl_->cancelled,.max_body_bytes=64*1024,.follow_redirects=false,
             .timeout_ms=15000,.probe_verification_headers=true,.cookie_session=impl_->cookies});
        auto result=ParsePassportReply(step,impl_->profile,reply.status_code,reply.body);
        if(reply.aigis_header_present && result.status!=QrStatus::Ready &&
           result.status!=QrStatus::Waiting && result.status!=QrStatus::Scanned &&
           result.status!=QrStatus::Confirmed) result.status=QrStatus::VerificationRequired;
        return result;
    } catch(...) {failed.status=QrStatus::TransportError;return failed;}
}
} // namespace cloudgenshin::auth

namespace cloudgenshin {
namespace {
auth::PassportIdentity NewPassportIdentity() {
    std::array<unsigned char,21> bytes{};
#ifdef __SWITCH__
    randomGet(bytes.data(),bytes.size());
#else
    std::random_device random;for(auto& b:bytes) b=static_cast<unsigned char>(random());
#endif
    bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
    constexpr char hex[]="0123456789abcdef";
    auth::PassportIdentity identity;
    for(std::size_t i=0;i<16;++i) {
        if(i==4 || i==6 || i==8 || i==10) identity.device_id+='-';
        identity.device_id+=hex[bytes[i]>>4];identity.device_id+=hex[bytes[i]&15];
    }
    for(std::size_t i=16;i<bytes.size();++i) {
        identity.lifecycle_id+=hex[bytes[i]>>4];identity.lifecycle_id+=hex[bytes[i]&15];
    }
    return identity;
}
}
std::optional<Authorization> LoginPassport(const GameProfile& profile,const std::atomic_bool& cancelled,
    const std::function<void(const std::string&)>& render_qr,const Progress& progress) {
    const auto identity=NewPassportIdentity();
    auth::PassportSession session(profile,identity,cancelled);
    return RunPassportLogin(profile,identity,cancelled,render_qr,progress,
        {[&session](auth::PassportStep step,const std::string& ticket){return session.request(step,ticket);},
         [&cancelled](std::chrono::milliseconds delay){
            const auto until=std::chrono::steady_clock::now()+delay;
            while(!cancelled && std::chrono::steady_clock::now()<until)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return !cancelled;
         }});
}
} // namespace cloudgenshin
