#pragma once
#include "http_transfer_diagnostics.hpp"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace opennow::websocket {
struct Endpoint {
    std::string host_header,request_target,curl_url;
};
inline std::optional<Endpoint> ParseEndpoint(std::string_view url) {
    const bool secure=url.starts_with("wss://");
    const std::size_t authority_start=secure ? 6 : 5;
    if ((!secure && !url.starts_with("ws://")) || url.size()>8192 || url.find('#')!=url.npos)
        return std::nullopt;
    for (const unsigned char c:url) if (c<=0x20 || c==0x7f) return std::nullopt;
    const auto end=url.find_first_of("/?",authority_start);
    const auto authority=url.substr(authority_start,end==url.npos ? url.npos : end-authority_start);
    if (authority.empty() || authority.find('@')!=authority.npos) return std::nullopt;
    if (authority.front()=='[') {
        const auto bracket=authority.find(']');
        if (bracket==authority.npos || bracket==1 ||
            (bracket+1<authority.size() && authority[bracket+1]!=':')) return std::nullopt;
    } else if (authority.front()==':' || std::count(authority.begin(),authority.end(),':')>1) {
        return std::nullopt;
    }
    std::string target=end==url.npos ? "/" : std::string(url.substr(end));
    if (target.front()=='?') target='/'+target;
    return Endpoint{std::string(authority),std::move(target),
        std::string(secure ? "https://" : "http://")+std::string(url.substr(authority_start))};
}
// Digits-only port extraction for safe diagnostics; host, path and query never
// leave the transport layer.
inline int EndpointPort(std::string_view host_header,bool secure) {
    const int fallback=secure ? 443 : 80;
    const auto colon=host_header.rfind(':');
    if(colon==std::string_view::npos) return fallback;
    if(host_header.front()=='[') {
        const auto bracket=host_header.find(']');
        if(bracket==std::string_view::npos || colon<bracket) return fallback;
    }
    if(colon+1>=host_header.size() || colon+6<host_header.size()) return fallback;
    int port=0;
    for(std::size_t i=colon+1;i<host_header.size();++i) {
        const char c=host_header[i];
        if(c<'0' || c>'9') return fallback;
        port=port*10+(c-'0');
    }
    return port>0 && port<=65535 ? port : fallback;
}
inline long UpgradeHttpStatus(std::string_view response) {
    const auto end=response.find("\r\n");
    if (end==response.npos) return 0;
    const auto line=response.substr(0,end);
    if (!line.starts_with("HTTP/1.1 ") && !line.starts_with("HTTP/1.0 ")) return 0;
    if (line.size()<12 || (line.size()>12 && line[12]!=' ')) return 0;
    long code=0;
    for (std::size_t i=9;i<12;++i) {
        if (line[i]<'0' || line[i]>'9') return 0;
        code=code*10+(line[i]-'0');
    }
    return code>=100 && code<=599 ? code : 0;
}
enum class ConnectStage {
    NotStarted,Cancelled,EndpointInvalid,InitFailed,TlsConfigurationFailed,
    TransportFailed,HandshakeSendFailed,HandshakeReadFailed,UpgradeHttpRejected,
    UpgradeInvalid,UpgradeTooLarge,HandshakeTimeout,Ready
};
struct ConnectDiagnostics {
    ConnectStage stage=ConnectStage::NotStarted;
    int curl_code=0;
    long http_status=0;
    int endpoint_port=0;
    int elapsed_ms=0;
    // Transport-level failure text for autotest diagnostics only. Callers that
    // export it must bound/trim it; the safe event log itself only carries
    // integer codes.
    std::string detail{};
    // Host:port and request path of the attempted endpoint. The query string
    // (which carries the ephemeral session id) is never copied here.
    std::string endpoint_host{}, endpoint_path{};
    // Numeric transport timings only; useful when all RTT targets time out.
    HttpTransferDiagnostics transfer{};
    int os_error=0;
    const char* event() const {
        switch (stage) {
            case ConnectStage::Cancelled: return "SIGNALING_CONNECT_CANCELLED";
            case ConnectStage::EndpointInvalid: return "SIGNALING_ENDPOINT_INVALID";
            case ConnectStage::InitFailed: return "SIGNALING_TRANSPORT_INIT_FAILED";
            case ConnectStage::TlsConfigurationFailed: return "SIGNALING_TLS_CONFIG_FAILED";
            case ConnectStage::TransportFailed: return "SIGNALING_TCP_TLS_FAILED";
            case ConnectStage::HandshakeSendFailed: return "SIGNALING_UPGRADE_SEND_FAILED";
            case ConnectStage::HandshakeReadFailed: return "SIGNALING_UPGRADE_READ_FAILED";
            case ConnectStage::UpgradeHttpRejected: return "SIGNALING_UPGRADE_HTTP_REJECTED";
            case ConnectStage::UpgradeInvalid: return "SIGNALING_UPGRADE_INVALID";
            case ConnectStage::UpgradeTooLarge: return "SIGNALING_UPGRADE_TOO_LARGE";
            case ConnectStage::HandshakeTimeout: return "SIGNALING_UPGRADE_TIMEOUT";
            case ConnectStage::Ready: return "SIGNALING_UPGRADE_OK";
            default: return "SIGNALING_CONNECT_NOT_STARTED";
        }
    }
    bool may_try_alternative() const {
        // Never retry a permission/risk/rate-limit rejection or malformed 101.
        // Alternate addresses must come from the same service dispatch, and
        // each must independently pass ordinary TLS and Upgrade validation.
        return stage==ConnectStage::TransportFailed || stage==ConnectStage::HandshakeTimeout ||
            stage==ConnectStage::HandshakeReadFailed || stage==ConnectStage::HandshakeSendFailed ||
            (stage==ConnectStage::UpgradeHttpRejected && (http_status==502 || http_status==503 || http_status==504));
    }
};
inline std::vector<std::string> SecureEndpoints(const std::string& primary,const std::vector<std::string>& alternatives) {
    std::vector<std::string> result;
    const auto add=[&](const std::string& url) {
        if (result.size()<4 && url.starts_with("wss://") && ParseEndpoint(url) &&
            std::find(result.begin(),result.end(),url)==result.end()) result.push_back(url);
    };
    add(primary);
    for (const auto& alternative:alternatives) add(alternative);
    return result;
}
} // namespace opennow::websocket
