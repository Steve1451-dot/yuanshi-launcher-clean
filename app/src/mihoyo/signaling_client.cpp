#include "mihoyo/signaling_client.hpp"
#include "mihoyo/signaling_endpoint.hpp"

#include <utility>

namespace cloudgenshin {

namespace {
constexpr auto kKeepaliveInterval = std::chrono::seconds(10);
constexpr char kClientHello[] = R"({"client_type":"web","type":"client hello"})";
}

SignalingClient::SignalingClient(const std::string& wss_url,std::vector<std::string> alternatives)
    : socket_(wss_url),endpoints_(GameSignalingEndpoints(wss_url,alternatives)) {
    // Public production web SDK uses browser WebSocket, which sends this
    // page's Origin automatically. No cookie/account header is copied.
    socket_.set_custom_headers({"Origin: https://ys.mihoyo.com",
        "User-Agent: CloudGenshin-NX (Nintendo Switch; native WebRTC)"});
    socket_.set_on_binary_message([this](const std::vector<std::uint8_t>& bytes) {
        const auto frame = signaling::DecodeFrame(bytes);
        if (!frame) {
            local_error_ = "Invalid cloud signaling frame";
            socket_.disconnect();
            return;
        }
        if (frame->type == signaling::MessageType::Signaling && on_rtc_signal_) {
            if (const auto signal = rtc::ParseSignalMessage(frame->payload))
                on_rtc_signal_(*signal);
        }
        if (on_frame_) on_frame_(*frame);
    });
    socket_.set_on_message([this](const std::string& message) {
        if (message.empty()) return; // Official keepalive is empty text.
        local_error_ = "Unexpected text cloud signaling frame";
        socket_.disconnect();
    });
}

bool SignalingClient::send_answer(const std::string& sdp) {
    const auto payload = rtc::EncodeAnswer(sdp);
    return payload && send(signaling::MessageType::Signaling, *payload);
}

bool SignalingClient::send_candidate(const std::string& candidate,
                                     const std::string& sdp_mid, int sdp_mline_index) {
    const auto payload = rtc::EncodeCandidate(candidate, sdp_mid, sdp_mline_index);
    return payload && send(signaling::MessageType::Signaling, *payload);
}

bool SignalingClient::connect(bool send_hello, const std::atomic_bool* cancelled) {
    connected_endpoint_.clear();
    local_error_.clear();
    connect_diagnostics_={};
    if (endpoints_.empty()) {
        local_error_ = "Cloud signaling requires a secure WSS endpoint";
        connect_diagnostics_.stage=opennow::websocket::ConnectStage::EndpointInvalid;
        if (on_connect_attempt_) on_connect_attempt_(1,connect_diagnostics_);
        return false;
    }
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    for (std::size_t i=0;i<endpoints_.size();++i) {
        const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();
        if (remaining<100) break;
        socket_.reset_endpoint(endpoints_[i]);
        const bool connected=socket_.connect(cancelled,static_cast<long>(std::min<std::int64_t>(remaining,6000)));
        connect_diagnostics_=socket_.connect_diagnostics();
        if (on_connect_attempt_) on_connect_attempt_(static_cast<unsigned>(i+1),connect_diagnostics_);
        if (connected) {
            connected_endpoint_=endpoints_[i];
            if (send_hello && !send_client_hello()) {socket_.disconnect();return false;}
            next_keepalive_ = std::chrono::steady_clock::now() + kKeepaliveInterval;
            return true;
        }
        if (!connect_diagnostics_.may_try_alternative()) break;
    }
    return false;
}

bool SignalingClient::send_client_hello() {
    return send(signaling::MessageType::Handshake,
        {kClientHello, kClientHello + sizeof(kClientHello) - 1});
}

void SignalingClient::disconnect() {
    socket_.disconnect();
}

bool SignalingClient::send(signaling::MessageType type,
                           const std::vector<std::uint8_t>& payload) {
    if (!is_connected()) return false;
    if (type == signaling::MessageType::Keepalive && payload.empty()) {
        socket_.send_message("");
        return is_connected();
    }
    const auto bytes = signaling::EncodeFrame(type, payload);
    if (bytes.empty()) {
        local_error_ = "Invalid cloud signaling payload";
        return false;
    }
    return socket_.send_binary(bytes);
}

void SignalingClient::poll() {
    socket_.poll();
    if (!is_connected()) return;
    const auto now = std::chrono::steady_clock::now();
    if (now >= next_keepalive_) {
        if (send(signaling::MessageType::Keepalive, {}))
            next_keepalive_ = now + kKeepaliveInterval;
    }
}

std::string SignalingClient::last_error() const {
    return local_error_.empty() ? socket_.get_last_error() : local_error_;
}

} // namespace cloudgenshin
