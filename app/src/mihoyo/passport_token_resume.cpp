#include "mihoyo/passport_token_resume.hpp"
#include "mihoyo/passport_qr_login.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/sdk_json.hpp"
#include "mihoyo/passport_signin_policy.hpp"
#include "http_client.hpp"
#include <mbedtls/md5.h>
#include <algorithm>
#include <array>
#include <ctime>
#include <limits>
#include <random>
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin {
namespace {
using Json=nlohmann::json;
bool CookieValue(const std::string& value,std::size_t limit=4096) {
    return !value.empty() && value.size()<=limit &&
        std::all_of(value.begin(),value.end(),[](unsigned char c) {
            return (c>='a' && c<='z') || (c>='A' && c<='Z') || (c>='0' && c<='9') ||
                c=='_' || c=='-' || c=='.' || c=='~' || c=='+' || c=='/' || c=='=';
        });
}
std::string Text(const Json& object,const char* name) {
    if(!object.is_object() || !object.contains(name) || !object[name].is_string()) return {};
    const auto& text=object[name].get_ref<const std::string&>();
    return CookieValue(text) ? text : std::string{};
}
std::string Aid(const Json& object) {
    if(!object.is_object() || !object.contains("aid")) return {};
    const auto& value=object["aid"];
    if(value.is_number_unsigned()) return std::to_string(value.get<std::uint64_t>());
    if(value.is_number_integer() && value.get<std::int64_t>()>0)
        return std::to_string(value.get<std::int64_t>());
    return Text(object,"aid");
}
std::string Signature(const std::string& body) {
    // RequestUtils.createSign(), official Android passport SDK 2.54.0.
    std::array<unsigned char,6> bytes{};
#ifdef __SWITCH__
    randomGet(bytes.data(),bytes.size());
#else
    std::random_device random;for(auto& b:bytes) b=static_cast<unsigned char>(random());
#endif
    constexpr char alphabet[]="abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";
    std::string nonce;for(const auto b:bytes) nonce+=alphabet[b%62];
    const auto time=std::to_string(std::time(nullptr));
    const auto input="salt=JwYDpKvLj6MrMqqYU6jTKF17KNO2PXoS&t="+time+"&r="+nonce+"&b="+body+"&q=";
    unsigned char digest[16]{};
    if(mbedtls_md5(reinterpret_cast<const unsigned char*>(input.data()),input.size(),digest)!=0) return {};
    constexpr char hex[]="0123456789abcdef";
    std::string output=time+","+nonce+",";
    for(const auto byte:digest) {output+=hex[byte>>4];output+=hex[byte&15];}
    return output;
}
bool NeedsVerification(const Json& data) {
    for(const char* key:{"realname_info","reactivate_info"})
        if(data.contains(key) && data[key].is_object() && data[key].value("required",Json(false))!=false) return true;
    return data.contains("need_realperson") && !data["need_realperson"].is_null() && data["need_realperson"]!=false;
}
std::string Token(const Json& data,int type) {
    if(!data.is_object() || !data.contains("token") || !data["token"].is_object() ||
       data["token"].value("token_type",Json())!=type) return {};
    return Text(data["token"],"token");
}
int RejectionHint(const Json& root) {
    // Fixed phrase bits only, never arbitrary server text. These are hints,
    // not permission to retry, change identity, or bypass a risk decision.
    int bits=0;
    for(const char* key:{"message","msg"}) {
        if(!root.contains(key) || !root[key].is_string()) continue;
        std::string text=root[key].get<std::string>();if(text.size()>4096) continue;
        for(auto& c:text) if(c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
        const auto has=[&](std::string_view phrase){return text.find(phrase)!=text.npos;};
        if(has("expired") || has("过期") || has("重新登录")) bits|=1;
        if(has("invalid token") || has("token invalid") || has("token无效") || has("无效令牌")) bits|=2;
        if(has("signature") || has("签名") || has("invalid ds")) bits|=4;
        if(has("parameter") || has("参数")) bits|=8;
        if(has("too many") || has("频繁") || has("频率")) bits|=16;
        if(has("captcha") || has("verification") || has("验证") || has("风险") || has("设备")) bits|=32;
        if(has("稍后再试") || has("try again") || has("unavailable")) bits|=64;
    }
    return bits;
}
}

std::optional<Authorization> ResumePassportFromAccount(const GameProfile& profile,
    const Authorization& saved,const std::atomic_bool& cancelled,const Progress& progress,PassportResumeSource source) {
    // This bridge experiment is deliberately hkrpg-only. A supplied lookalike
    // profile cannot redirect credentials even before endpoint construction.
    if(cancelled) return {};
    const auto* canonical=FindGameProfile("hkrpg");
    if(!canonical || profile.id!="hkrpg" || profile.sdk_base!=canonical->sdk_base ||
       profile.cloud_base!=canonical->cloud_base || profile.app_id!=8 || profile.game_biz!="hkrpg_cn" ||
       saved.combo.app_id!="8" || saved.combo.channel_id!="1" || saved.combo.open_id!=saved.account_id ||
       saved.account_id.empty() || saved.account_id.size()>32 || saved.account_id.front()=='0' ||
       !std::all_of(saved.account_id.begin(),saved.account_id.end(),[](char c){return c>='0' && c<='9';}) ||
       !CookieValue(saved.channel_token) || !CookieValue(saved.device.device_id,128) ||
       (!saved.passport_mid.empty() && !auth::ValidPassportMid(saved.passport_mid)) ||
       saved.device.user_agent.empty() || saved.device.user_agent.find_first_of("\r\n")!=std::string::npos) {
        progress("GAME_WEB_RESUME_UNAVAILABLE",0,0);return {};
    }
    const auth::PassportIdentity identity{saved.device.device_id,saved.device.device_id};
    auto app_headers=auth::BuildPassportHeaders(profile,identity,false);
    if(!app_headers) return {};
    // Public Android production values: server_env_config.json + RequestUtils.
    for(auto& header:*app_headers) {
        if(header.starts_with("x-rpc-app_id:")) header="x-rpc-app_id: c90mr1bwo2rk";
        if(header.starts_with("x-rpc-client_type:")) header="x-rpc-client_type: 2";
        if(header.starts_with("x-rpc-sdk_version:")) header="x-rpc-sdk_version: 2.54.0";
        if(header.starts_with("x-rpc-device_os:")) header="x-rpc-sys_version: Horizon";
    }
    std::erase_if(*app_headers,[](const std::string& header) {
        return header.starts_with("Origin:") || header.starts_with("Referer:") || header.starts_with("x-rpc-mi_referrer:");
    });
    app_headers->push_back("Accept: application/json");
    app_headers->push_back("x-rpc-account_version: 2.54.0");
    app_headers->push_back("x-rpc-app_version: "+std::string(profile.app_version));
    const auto request=[&](int stage,const std::string& url,std::vector<std::string> headers,
                           const std::string& body)->std::optional<opennow::HttpResponse> {
        if(cancelled) return {};
        progress("GAME_WEB_RESUME_STAGE",stage,0);
        try {
            auto response=opennow::HttpClient{}.Post(url,saved.device.user_agent,headers,body,{},
                {.cancelled=&cancelled,.max_body_bytes=64*1024,.follow_redirects=false,.timeout_ms=15000,
                 .probe_verification_headers=true});
            if(cancelled) return {};
            if(response.aigis_header_present) {
                progress("GAME_WEB_RESUME_VERIFICATION_REQUIRED",0,response.status_code);return {};
            }
            if(response.status_code<200 || response.status_code>=300) {
                progress("GAME_WEB_RESUME_HTTP_FAILED",0,response.status_code);return {};
            }
            return response;
        } catch(const opennow::HttpRequestError& error) {
            if(!cancelled) progress("GAME_WEB_RESUME_NETWORK_FAILED",static_cast<int>(error.curl_code),error.http_status);
        } catch(...) {if(!cancelled) progress("GAME_WEB_RESUME_NETWORK_FAILED",0,0);}
        return {};
    };
    const auto parse=[&](const opennow::HttpResponse& response)->std::optional<Json> {
        if(response.body.size()>64*1024) return {};
        auto root=sdk::ParseSdkJson(response.body);
        if(!root.is_object() || !root.contains("retcode") || !root["retcode"].is_number_integer()) return {};
        const auto& code=root["retcode"];
        if(code!=0) {
            int numeric=0;
            if(code.is_number_unsigned()) {
                const auto value=code.get<std::uint64_t>();
                if(value<=static_cast<std::uint64_t>(std::numeric_limits<int>::max())) numeric=static_cast<int>(value);
            } else {
                const auto value=code.get<std::int64_t>();
                if(value>=std::numeric_limits<int>::min() && value<=std::numeric_limits<int>::max()) numeric=static_cast<int>(value);
            }
            if(const int hint=RejectionHint(root)) progress("GAME_WEB_RESUME_REJECTION_HINT_MASK",hint,0);
            progress("GAME_WEB_RESUME_API_REJECTED",numeric,response.status_code);return {};
        }
        if(!root.contains("data") || !root["data"].is_object()) return {};
        if(NeedsVerification(root["data"])) {
            progress("GAME_WEB_RESUME_VERIFICATION_REQUIRED",0,response.status_code);return {};
        }
        return root["data"];
    };
    if(source!=PassportResumeSource::SToken && source!=PassportResumeSource::GameToken) return {};
    progress("GAME_WEB_RESUME_SOURCE",static_cast<int>(source),0);
    const bool from_game=source==PassportResumeSource::GameToken;
    std::string stoken=saved.channel_token,mid=saved.passport_mid;
    const bool retained_context=!from_game && auth::ValidPassportMid(mid);
    if(!retained_context) {
    auto headers=*app_headers;
    std::string upgrade_body;
    if(from_game) {
        // LoginManager.loginByGameToken uses numeric account_id, not a Cookie.
        // Do not guess the source automatically or retry a rejected SToken.
        std::uint64_t numeric=0;
        for(const auto c:saved.account_id) {
            if(numeric>(static_cast<std::uint64_t>(std::numeric_limits<int>::max())-(c-'0'))/10) {
                progress("GAME_WEB_RESUME_UNAVAILABLE",16,0);return {};
            }
            numeric=numeric*10+(c-'0');
        }
        upgrade_body=Json{{"account_id",numeric},{"game_token",saved.channel_token}}.dump();
    } else headers.push_back("Cookie: stuid="+saved.account_id+";stoken="+saved.channel_token);
    const auto upgrade_sign=Signature(from_game ? upgrade_body : "{}");
    if(upgrade_sign.empty()) return {};
    headers.push_back("DS: "+upgrade_sign);
    // @POST with no RequestBody; the SDK still signs the empty parameter map.
    const auto upgraded=request(1,from_game ?
        "https://passport-api.mihoyo.com/account/ma-cn-session/app/getTokenByGameToken" :
        "https://passport-api.mihoyo.com/account/ma-cn-session/app/getTokenBySToken",headers,upgrade_body);
    const auto account=upgraded ? parse(*upgraded) : std::nullopt;
    if(!account) {if(upgraded) progress("GAME_WEB_RESUME_FAILED",1,upgraded->status_code);return {};}
    stoken=Token(*account,1);
    if(!account->contains("user_info") || !(*account)["user_info"].is_object() || stoken.empty()) {
        progress("GAME_WEB_RESUME_FAILED",1,upgraded->status_code);return {};
    }
    mid=Text((*account)["user_info"],"mid");
    if(Aid((*account)["user_info"])!=saved.account_id || !auth::ValidPassportMid(mid)) {
        progress("GAME_WEB_RESUME_ACCOUNT_MISMATCH",1,upgraded->status_code);return {};
    }
    }
    const auto body=Json{{"dst_token_type",4},{"src_token",{{"token",stoken},{"token_type",1}}},{"mid",mid}}.dump();
    auto headers=*app_headers;
    const auto exchange_sign=Signature(body);if(exchange_sign.empty()) return {};
    headers.push_back("DS: "+exchange_sign);
    const auto exchanged=request(2,"https://passport-api.mihoyo.com/account/ma-cn-session/app/exchange",headers,body);
    const auto cookie_data=exchanged ? parse(*exchanged) : std::nullopt;
    if(!cookie_data) {if(exchanged) progress("GAME_WEB_RESUME_FAILED",2,exchanged->status_code);return {};}
    if(cookie_data->contains("user_info") && (*cookie_data)["user_info"].is_object() &&
       (Aid((*cookie_data)["user_info"])!=saved.account_id || Text((*cookie_data)["user_info"],"mid")!=mid)) {
        progress("GAME_WEB_RESUME_ACCOUNT_MISMATCH",2,exchanged->status_code);return {};
    }
    const auto cookie=Token(*cookie_data,4);
    if(cookie.empty()) {progress("GAME_WEB_RESUME_FAILED",2,exchanged->status_code);return {};}
    // CookieManagerUtil's official account_v2 names. Every value was issued by
    // the official service for this verified account. No fabricated mid/token.
    const auto cookie_header="Cookie: account_id_v2="+saved.account_id+";account_mid_v2="+mid+";cookie_token_v2="+cookie;
    const auto web_request=[&](int stage,auth::PassportStep step,const std::string& url)->std::optional<auth::PassportReply> {
        auto web_headers=auth::BuildPassportHeaders(profile,identity,step==auth::PassportStep::Combo);
        const auto web_body=auth::BuildPassportBody(step,profile,{});
        if(!web_headers || !web_body) return {};
        web_headers->push_back(cookie_header);
        const auto reply=request(stage,url,*web_headers,*web_body);
        if(!reply) return {};
        auto parsed=auth::ParsePassportReply(step,profile,reply->status_code,reply->body);
        if(parsed.status!=auth::QrStatus::Confirmed) {
            progress(parsed.status==auth::QrStatus::VerificationRequired || parsed.status==auth::QrStatus::RiskRejected ?
                "GAME_WEB_RESUME_VERIFICATION_REQUIRED" : "GAME_WEB_RESUME_FAILED",parsed.retcode,parsed.http_status);
            return {};
        }
        if(parsed.account_id!=saved.account_id) {
            progress("GAME_WEB_RESUME_ACCOUNT_MISMATCH",stage,parsed.http_status);return {};
        }
        return parsed;
    };
    const auto verified=web_request(3,auth::PassportStep::Verify,
        "https://passport-api.mihoyo.com/account/ma-cn-session/web/webVerifyForGame");
    if(!verified) return {};
    if(!verified->passport_mid.empty() && verified->passport_mid!=mid) {
        progress("GAME_WEB_RESUME_ACCOUNT_MISMATCH",3,verified->http_status);return {};
    }
    auto combo=web_request(4,auth::PassportStep::Combo,
        "https://hkrpg-sdk.mihoyo.com/hkrpg_cn/combo/granter/login/webLogin");
    if(!combo || !combo->combo || combo->combo->app_id!="8" || combo->combo->channel_id!="1" ||
       combo->combo->open_id!=saved.combo.open_id) return {};
    Authorization result=saved;
    result.combo=std::move(*combo->combo);result.channel_token=verified->channel_token;result.passport_mid=mid;
    progress("GAME_WEB_RESUME_OK",0,combo->http_status);
    return result;
}
} // namespace cloudgenshin
