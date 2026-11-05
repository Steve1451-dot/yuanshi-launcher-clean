#include "mihoyo/passport_qr_login.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/combo_exchange.hpp"
#include "mihoyo/passport_signin_policy.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <limits>

namespace cloudgenshin::auth {
namespace {
using Json = nlohmann::json;
bool SafeText(const std::string& value, std::size_t max) {
    return !value.empty() && value.size() <= max &&
        std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=0x21 && c<=0x7e;});
}
bool NumericId(const std::string& value) {
    return !value.empty() && value.size()<=32 && value.front()!='0' &&
        std::all_of(value.begin(),value.end(),[](char c){return c>='0' && c<='9';});
}
bool NeedsHumanVerification(const Json& data) {
    for(const auto key:{"realname_info","reactivate_info"})
        if(data.contains(key) && data[key].is_object() && data[key].value("required",Json(false))!=false)
            return true;
    return data.contains("need_realperson") && data["need_realperson"]!=false;
}
}
bool SupportsPassportLogin(const GameProfile& profile) {
    // Every miHoYo CN cloud title shares the same passport login-platform flow;
    // only the Combo exchange target (sdk_base / app_id / game_biz) differs.
    const std::string_view sdk = profile.sdk_base;
    return (profile.game_biz=="hk4e_cn" && profile.app_id==4 &&
            sdk.starts_with("https://hk4e-sdk.mihoyo.com/")) ||
           (profile.game_biz=="hkrpg_cn" && profile.app_id==8 &&
            sdk.starts_with("https://hkrpg-sdk.mihoyo.com/"));
}
std::optional<std::vector<std::string>> BuildPassportHeaders(
    const GameProfile& profile,const PassportIdentity& identity,bool combo) {
    if(!SupportsPassportLogin(profile) || !SafeText(identity.device_id,128) ||
       !SafeText(identity.lifecycle_id,64) ||
       (!identity.device_fp.empty() && !SafeText(identity.device_fp,128))) return std::nullopt;
    // Official sr web.4f0790db.js loginPlatform.appId is distinct from the
    // Genshin login platform. Combo app_id (8/4) remains a separate header.
    const std::string passport_app=profile.id=="hkrpg" ? "c90mr1bwo2rk" : "c76ync6mutq8";
    std::vector<std::string> headers{
        "Content-Type: application/json", "Origin: https://user.mihoyo.com",
        "Referer: https://user.mihoyo.com/", "x-rpc-app_id: "+passport_app,
        "x-rpc-game_biz: "+std::string(profile.game_biz),
        "x-rpc-client_type: 22", "x-rpc-sdk_version: 2.57.0",
        "x-rpc-device_id: "+identity.device_id, "x-rpc-lifecycle_id: "+identity.lifecycle_id,
        "x-rpc-device_model: Nintendo%20Switch", "x-rpc-device_name: Nintendo%20Switch",
        "x-rpc-device_os: Horizon", "x-rpc-language: zh-cn",
        "x-rpc-mi_referrer: https://user.mihoyo.com/login-platform/index.html?client_type=22&app_id="+passport_app+
            (profile.id=="hk4e" ? "&theme=ys" : "")+"&token_type=4&game_biz="+std::string(profile.game_biz)+"#/login/qr"};
    if(!identity.device_fp.empty()) headers.push_back("x-rpc-device_fp: "+identity.device_fp);
    if(combo) {
        // The official combo SDK owns these request headers. Cookie scope is
        // still enforced by libcurl; no credentials are put in a header here.
        for(auto& header:headers) {
            if(header.starts_with("x-rpc-app_id:")) header="x-rpc-app_id: "+AppIdText(profile);
            if(header.starts_with("x-rpc-sdk_version:")) header="x-rpc-mdk_version: 2.55.0";
        }
        headers.push_back("x-rpc-channel_id: 1");
    }
    return headers;
}
std::optional<std::string> BuildPassportBody(PassportStep step,const GameProfile& profile,
                                           const std::string& ticket) {
    if(!SupportsPassportLogin(profile)) return std::nullopt;
    if(step==PassportStep::Create) return "{}";
    if(step==PassportStep::Verify) return std::string(); // official request has no body
    if(step==PassportStep::Combo) return Json{{"app_id",profile.app_id},{"channel_id",1}}.dump();
    if(!SafeText(ticket,128) || ticket.find_first_of("\"\\;?&#")!=std::string::npos) return std::nullopt;
    return Json{{"ticket",ticket}}.dump();
}
PassportReply ParsePassportReply(PassportStep step,const GameProfile& profile,
                                 long http,const std::string& body) {
    PassportReply out; out.http_status=http;
    if(http<200 || http>=300) {out.status=QrStatus::HttpError;return out;}
    if(!SupportsPassportLogin(profile) || body.empty() || body.size()>64*1024) return out;
    const auto root=Json::parse(body,nullptr,false);
    if(!root.is_object() || !root.contains("retcode") || !root["retcode"].is_number_integer()) return out;
    const auto& code=root["retcode"];
    if(code.is_number_unsigned()) {
        const auto value=code.get<std::uint64_t>();
        if(value>static_cast<std::uint64_t>(std::numeric_limits<int>::max())) return out;
        out.retcode=static_cast<int>(value);
    } else {
        const auto value=code.get<std::int64_t>();
        if(value<std::numeric_limits<int>::min() || value>std::numeric_limits<int>::max()) return out;
        out.retcode=static_cast<int>(value);
    }
    if(out.retcode) {
        out.status=QrStatus::Rejected;
        if(out.retcode==-3501 || out.retcode==-3503 || out.retcode==-3504) out.status=QrStatus::Expired;
        if(out.retcode==-3505) out.status=QrStatus::Cancelled;
        if(out.retcode==-3506) out.status=QrStatus::RiskRejected;
        if(out.retcode==-3101 || out.retcode==-3235) out.status=QrStatus::VerificationRequired;
        return out;
    }
    if(!root.contains("data") || !root["data"].is_object()) return out;
    const auto& data=root["data"];
    if(step==PassportStep::Create) {
        if(!data.contains("url") || !data["url"].is_string() ||
           !data.contains("ticket") || !data["ticket"].is_string()) return out;
        const auto url=data["url"].get<std::string>(), ticket=data["ticket"].get<std::string>();
        // Do not render a server-supplied QR pointing to another origin, or
        // query using a different ticket from the one the phone confirms.
        constexpr std::string_view prefix="https://user.mihoyo.com/login-platform/mobile.html?";
        if(!SafeText(url,4096) || !url.starts_with(prefix) ||
           !BuildPassportBody(PassportStep::Query,profile,ticket)) return out;
        auto query=std::string_view(url).substr(prefix.size());
        // The official endpoint includes a mobile-page fragment route. It is
        // not part of the query/ticket; retain it for the phone's page router.
        query=query.substr(0,query.find('#'));
        bool found=false;
        while(!query.empty()) {
            const auto end=query.find('&');const auto pair=query.substr(0,end);
            const auto equal=pair.find('=');
            if(equal==std::string_view::npos || pair.substr(0,equal).find('%')!=std::string_view::npos) return out;
            if(pair.substr(0,equal)=="tk") {
                if(found || pair.substr(equal+1)!=ticket) return out;
                found=true;
            }
            if(end==std::string_view::npos) break;
            query.remove_prefix(end+1);
        }
        if(!found) return out;
        out.challenge=QrChallenge{url,ticket};out.status=QrStatus::Ready;return out;
    }
    if(step==PassportStep::Query) {
        if(!data.contains("status") || !data["status"].is_string()) return out;
        const auto status=data["status"].get<std::string>();
        if(status=="Created") out.status=QrStatus::Waiting;
        else if(status=="Scanned") out.status=QrStatus::Scanned;
        else if(status=="Expired") out.status=QrStatus::Expired;
        else if(status=="Confirmed") out.status=NeedsHumanVerification(data) ? QrStatus::VerificationRequired : QrStatus::Confirmed;
        return out;
    }
    if(step==PassportStep::Verify) {
        if(NeedsHumanVerification(data)) {out.status=QrStatus::VerificationRequired;return out;}
        if(!data.contains("token") || !data["token"].is_object() ||
           !data["token"].contains("token_type") || data["token"]["token_type"]!=1 ||
           !data["token"].contains("token") || !data["token"]["token"].is_string() ||
           !data.contains("user_info") || !data["user_info"].is_object() ||
           !data["user_info"].contains("aid") || !data["user_info"]["aid"].is_string()) return out;
        const auto token=data["token"]["token"].get<std::string>();
        const auto aid=data["user_info"]["aid"].get<std::string>();
        if(!SafeText(token,4096) || !NumericId(aid)) return out;
        if(data["user_info"].contains("mid")) {
            if(!data["user_info"]["mid"].is_string()) return out;
            const auto mid=data["user_info"]["mid"].get<std::string>();
            if(!ValidPassportMid(mid)) return out;
            out.passport_mid=mid;
        }
        out.account_id=aid;out.channel_token=token;out.status=QrStatus::Confirmed;return out;
    }
    const auto combo=ParseComboExchangeResponse(profile,http,body);
    out.status=combo.status;out.retcode=combo.retcode;out.combo=combo.authorization;
    out.account_id=combo.account_id;return out;
}
} // namespace cloudgenshin::auth
