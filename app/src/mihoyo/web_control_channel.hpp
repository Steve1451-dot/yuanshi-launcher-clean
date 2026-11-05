#pragma once
#include "WebSocketClient.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace cloudgenshin::rtc {
inline bool UsesWebControl(int type) {return type==1 || type==2;}
std::optional<std::uint32_t> ParseControlSession(std::span<const std::uint8_t> json);
std::string WebControlEndpoint(std::string base,std::uint32_t session);
std::vector<std::uint8_t> EncodeControlSync(std::uint16_t command,std::uint32_t session);
std::optional<std::uint32_t> DecodeControlSession(std::span<const std::uint8_t> body);

// Official GameControlChannel WebSocket fallback, also used when web transport
// type 2 is selected. Owns no thread; the session network worker polls it.
// Session id and endpoint are ephemeral authentication material, never logged.
class WebControlChannel {
public:
    using Clock=std::chrono::steady_clock;
    using Data=std::function<void(const std::uint8_t*,std::size_t)>;
    explicit WebControlChannel(Data data={}):socket_(""),data_(std::move(data)) {}
    bool start(const std::vector<std::string>& urls,std::uint32_t session,
               Clock::time_point now,const std::atomic_bool* cancelled);
    void poll(Clock::time_point now);
    void stop();
    bool ready() const {return ready_ && socket_.is_connected() && !error_;}
    bool send(const std::vector<std::uint8_t>& packet);
    const char* error() const {return error_;}
    const opennow::websocket::ConnectDiagnostics& connect_diagnostics() const {return diagnostics_;}
    // Last attempted endpoint without its query string (no session id); for the
    // opt-in autotest trace only.
    const std::string& attempted_url() const {return attempted_url_;}
private:
    void receive(const std::vector<std::uint8_t>& bytes);
    bool send_sync(std::uint16_t command);
    WebSocketClient socket_;
    Data data_;
    std::uint32_t session_=0;
    bool ready_=false;
    const char* error_=nullptr;
    opennow::websocket::ConnectDiagnostics diagnostics_;
    std::string attempted_url_;
    Clock::time_point now_{},deadline_{},next_ping_{},last_pong_{};
};
} // namespace cloudgenshin::rtc
