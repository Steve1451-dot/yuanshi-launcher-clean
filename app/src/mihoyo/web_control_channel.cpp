#include "mihoyo/web_control_channel.hpp"
#include "mihoyo/game_control.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <algorithm>
#include <limits>

namespace cloudgenshin::rtc {
std::optional<std::uint32_t> ParseControlSession(std::span<const std::uint8_t> bytes) {
    if(bytes.empty() || bytes.size()>8192) return std::nullopt;
    const auto json=nlohmann::json::parse(bytes.begin(),bytes.end(),nullptr,false);
    if(!json.is_object()) return std::nullopt;
    const auto id=json.find("session_id");
    if(id==json.end() || !id->is_number_integer()) return std::nullopt;
    if(!id->is_number_unsigned() && id->get<std::int64_t>()<=0) return std::nullopt;
    const auto value=id->get<std::uint64_t>();
    if(!value || value>std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
    return static_cast<std::uint32_t>(value);
}
std::string WebControlEndpoint(std::string base,std::uint32_t session) {
    if(base.starts_with("https://")) base.replace(0,8,"wss://");
    if(!session || !base.starts_with("wss://") || !opennow::websocket::ParseEndpoint(base)) return {};
    // Dispatch URLs normally have no query. Preserve any non-session query;
    // reject ambiguity instead of authenticating with an earlier sessionId.
    const auto query=base.find('?');
    if(query!=base.npos) {
        std::string_view fields(base.data()+query+1,base.size()-query-1);
        while(!fields.empty()) {
            const auto end=fields.find('&');const auto field=fields.substr(0,end);
            if(field.substr(0,field.find('='))=="sessionId") return {};
            if(end==fields.npos) break;
            fields.remove_prefix(end+1);
        }
    }
    base+=(query==base.npos ? "?" : base.ends_with('?') || base.ends_with('&') ? "" : "&");
    base+="sessionId="+std::to_string(session);
    return opennow::websocket::ParseEndpoint(base) ? base : std::string{};
}
std::vector<std::uint8_t> EncodeControlSync(std::uint16_t command,std::uint32_t session) {
    if(command<1250 || command>1254 || !session) return {};
    std::vector<std::uint8_t> body{8};std::uint32_t value=session;
    do {const auto b=static_cast<std::uint8_t>(value&127);value>>=7;body.push_back(b|(value ? 128 : 0));} while(value);
    auto packet=control::EncodePacket({command,{},std::move(body)});
    packet.insert(packet.begin(),0);return packet;
}
std::optional<std::uint32_t> DecodeControlSession(std::span<const std::uint8_t> body) {
    // These five SDK messages contain only uint32 field 1. Reject malformed
    // or duplicate session values; accept safely skippable future varints.
    if(body.size()>256) return std::nullopt;
    std::optional<std::uint32_t> session;
    std::size_t offset=0;
    const auto read=[&](std::uint64_t& out) {
        out=0;
        for(unsigned n=0;n<10 && offset<body.size();++n) {
            const auto b=body[offset++];if(n==9 && b>1) return false;
            out|=static_cast<std::uint64_t>(b&127)<<(7*n);if(!(b&128)) return true;
        }
        return false;
    };
    while(offset<body.size()) {
        std::uint64_t key,value;if(!read(key) || !key || (key&7)!=0 || !read(value)) return std::nullopt;
        if(key==8) {
            if(session || !value || value>std::numeric_limits<std::uint32_t>::max()) return std::nullopt;
            session=static_cast<std::uint32_t>(value);
        }
    }
    return session;
}

bool WebControlChannel::start(const std::vector<std::string>& urls,std::uint32_t session,
                              Clock::time_point now,const std::atomic_bool* cancelled) {
    stop();session_=session;now_=now;
    socket_.set_custom_headers({"Origin: https://ys.mihoyo.com",
        "User-Agent: CloudGenshin-NX (Nintendo Switch; native WebRTC)"});
    socket_.set_on_binary_message([this](const auto& bytes){receive(bytes);});
    socket_.set_on_message([this](const std::string&){error_="CONTROL_WSS_PROTOCOL_INVALID";});
    const auto connect_deadline=Clock::now()+std::chrono::seconds(6);
    std::vector<std::string> seen;
    for(const auto& base:urls) {
        const auto url=WebControlEndpoint(base,session);
        if(url.empty() || seen.size()>=4 || std::find(seen.begin(),seen.end(),url)!=seen.end()) continue;
        seen.push_back(url);
        attempted_url_=url.substr(0,url.find('?'));
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(connect_deadline-Clock::now()).count();
        if(remaining<=0 || (cancelled && *cancelled)) break;
        socket_.reset_endpoint(url);
        const bool connected=socket_.connect(cancelled,static_cast<long>(remaining));
        diagnostics_=socket_.connect_diagnostics();
        if(connected) {
            deadline_=now+std::chrono::seconds(15);
            if(send_sync(1250)) return true;
            error_="CONTROL_WSS_SEND_FAILED";return false;
        }
        if(!socket_.connect_diagnostics().may_try_alternative()) break;
    }
    error_=seen.empty() ? "CONTROL_WSS_ENDPOINT_MISSING" : "CONTROL_WSS_CONNECT_FAILED";
    return false;
}
void WebControlChannel::stop() {
    socket_.disconnect();session_=0;ready_=false;error_=nullptr;diagnostics_={};
    attempted_url_.clear();
    now_={};deadline_={};next_ping_={};last_pong_={};
}
bool WebControlChannel::send_sync(std::uint16_t command) {
    return socket_.send_binary(EncodeControlSync(command,session_));
}
bool WebControlChannel::send(const std::vector<std::uint8_t>& packet) {
    if(!ready() || !control::DecodePacket(packet)) return false;
    std::vector<std::uint8_t> frame{1};frame.insert(frame.end(),packet.begin(),packet.end());
    if(socket_.send_binary(frame)) return true;
    error_="CONTROL_WSS_SEND_FAILED";return false;
}
void WebControlChannel::receive(const std::vector<std::uint8_t>& bytes) {
    if(error_) return;
    if(bytes.empty() || bytes.size()>65536+4096+13 || bytes.front()>1) {error_="CONTROL_WSS_PROTOCOL_INVALID";return;}
    const auto packet=control::DecodePacket(std::span(bytes).subspan(1));
    if(!packet) {error_="CONTROL_WSS_PROTOCOL_INVALID";return;}
    if(bytes.front()==1) {
        if(ready_ && data_) data_(bytes.data()+1,bytes.size()-1);
        return;
    }
    if(packet->command!=1251 && packet->command!=1254) return;
    const auto id=DecodeControlSession(packet->body);
    if(!id || *id!=session_) {error_="CONTROL_WSS_SESSION_MISMATCH";return;}
    if(packet->command==1251) {
        if(!send_sync(1252)) {error_="CONTROL_WSS_SEND_FAILED";return;}
        if(!ready_) {ready_=true;last_pong_=now_;next_ping_=now_+std::chrono::seconds(2);}
    } else if(ready_) last_pong_=now_;
}
void WebControlChannel::poll(Clock::time_point now) {
    now_=now;if(error_) return;
    socket_.poll();
    if(error_) return;
    // The WebSocket poll reads fresh bytes after dispatching buffered frames.
    // At a deadline, dispatch those bytes once before declaring a timeout;
    // otherwise a pong already in the receive buffer is mistaken for silence.
    if((!ready_ && now>=deadline_) ||
       (ready_ && now-last_pong_>std::chrono::seconds(5))) socket_.poll();
    if(error_) return;
    if(!socket_.is_connected()) {error_="CONTROL_WSS_DISCONNECTED";return;}
    if(!ready_) {if(now>=deadline_) error_="CONTROL_WSS_SYNC_TIMEOUT";return;}
    if(now-last_pong_>std::chrono::seconds(5)) {error_="CONTROL_WSS_PONG_TIMEOUT";return;}
    if(now>=next_ping_) {
        if(!send_sync(1253)) error_="CONTROL_WSS_SEND_FAILED";
        next_ping_=now+std::chrono::seconds(2);
    }
}
} // namespace cloudgenshin::rtc
