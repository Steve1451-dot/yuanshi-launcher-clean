#include "WebSocketClient.hpp"
#include "websocket_handshake.hpp"
#include "http_client.hpp"
#include "curl_transfer_diagnostics.hpp"
#include <iostream>
#include <borealis/core/logger.hpp>
#include <array>
#include <cstring>
#include <cstdint>
#include <limits>
#include <random>
#include <thread>
#include <chrono>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace
{

constexpr size_t kMaximumHandshakeBytes = 64 * 1024;
constexpr size_t kMaximumSignalingPayloadBytes = 4 * 1024 * 1024;
constexpr size_t kMaximumPollReadBytes = 64 * 1024;
constexpr size_t kMaximumFramesPerPoll = 16;

std::vector<uint8_t> random_bytes(size_t length)
{
    std::vector<uint8_t> bytes(length);

#ifdef __SWITCH__
    randomGet(bytes.data(), bytes.size());
#else
    static thread_local std::mt19937 rng([] {
        std::random_device rd;
        const auto tick_seed = static_cast<uint32_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        return tick_seed ^ rd();
    }());

    std::uniform_int_distribution<int> dist(0, 255);
    for (uint8_t& byte : bytes)
        byte = static_cast<uint8_t>(dist(rng));
#endif

    return bytes;
}

std::string base64_encode(const uint8_t* data, size_t length) {
    std::string out;
    int val = 0, valb = -6;
    for (size_t i = 0; i < length; ++i) {
        uint8_t c = data[i];
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

} // namespace

WebSocketClient::WebSocketClient(const std::string& url) : url_(url) {}

WebSocketClient::~WebSocketClient() {
    disconnect();
    close_transport();
}

bool WebSocketClient::connect(const std::atomic_bool* cancelled,long timeout_ms) {
    using Stage=opennow::websocket::ConnectStage;
    connect_diagnostics_={};
    const auto started=std::chrono::steady_clock::now();
    struct FinishDiagnostic {
        opennow::websocket::ConnectDiagnostics& diagnostic;
        std::chrono::steady_clock::time_point started;
        ~FinishDiagnostic() {
            diagnostic.elapsed_ms=static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now()-started).count());
        }
    } finish_diagnostic{connect_diagnostics_,started};
    connect_cancelled_=cancelled;
    struct ResetCancel {
        const std::atomic_bool*& pointer;
        ~ResetCancel(){pointer=nullptr;}
    } reset_cancel{connect_cancelled_};
    if (cancelled && cancelled->load()) {
        connect_diagnostics_.stage=Stage::Cancelled;last_error_="WebSocket connect cancelled";return false;
    }
    disconnect();
    close_transport();

    last_error_.clear();
    timeout_ms=std::clamp(timeout_ms,100L,30000L);
    connect_deadline_=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
    const auto endpoint=opennow::websocket::ParseEndpoint(url_);
    if (!endpoint) {
        connect_diagnostics_.stage=Stage::EndpointInvalid;
        last_error_="Invalid WebSocket endpoint";return false;
    }
    connect_diagnostics_.endpoint_port=
        opennow::websocket::EndpointPort(endpoint->host_header,url_.starts_with("wss://"));
    connect_diagnostics_.endpoint_host=endpoint->host_header;
    connect_diagnostics_.endpoint_path=
        endpoint->request_target.substr(0,endpoint->request_target.find('?'));
    curl_ = curl_easy_init();
    if (!curl_) {
        connect_diagnostics_.stage=Stage::InitFailed;
        last_error_ = "Could not initialize WebSocket transport";
        return false;
    }

    curl_easy_setopt(curl_, CURLOPT_URL, endpoint->curl_url.c_str());
    curl_easy_setopt(curl_, CURLOPT_CONNECT_ONLY, 1L);
    curl_easy_setopt(curl_, CURLOPT_HTTP_VERSION, static_cast<long>(CURL_HTTP_VERSION_1_1));
    if (!opennow::ConfigureHttpTls(curl_)) {
        connect_diagnostics_.stage=Stage::TlsConfigurationFailed;
        last_error_ = "Could not configure TLS certificate verification";
        close_transport();
        return false;
    }
    curl_easy_setopt(curl_, CURLOPT_CONNECTTIMEOUT_MS, std::min(timeout_ms,15000L));
    curl_easy_setopt(curl_, CURLOPT_TIMEOUT_MS, timeout_ms);
    curl_easy_setopt(curl_, CURLOPT_NOSIGNAL, 1L);
    curl_error_buffer_[0]='\0';
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, curl_error_buffer_);
    if (cancelled) {
        curl_easy_setopt(curl_,CURLOPT_NOPROGRESS,0L);
        curl_easy_setopt(curl_,CURLOPT_XFERINFOFUNCTION,
            +[](void* user,curl_off_t,curl_off_t,curl_off_t,curl_off_t)->int {
                return static_cast<const std::atomic_bool*>(user)->load() ? 1 : 0;
            });
        curl_easy_setopt(curl_,CURLOPT_XFERINFODATA,cancelled);
    }

    CURLcode res = curl_easy_perform(curl_);
    connect_diagnostics_.transfer=opennow::CaptureCurlTransferDiagnostics(curl_,url_.starts_with("wss://"));
    long os_error=0;
    if(curl_easy_getinfo(curl_,CURLINFO_OS_ERRNO,&os_error)==CURLE_OK)
        connect_diagnostics_.os_error=static_cast<int>(std::clamp(os_error,0L,2147483647L));
    if (res != CURLE_OK) {
        connect_diagnostics_.stage=cancelled && cancelled->load() ? Stage::Cancelled : Stage::TransportFailed;
        connect_diagnostics_.curl_code=static_cast<int>(res);
        // Transport detail for the opt-in autotest trace; bounded printable
        // text, never the URL or any session material.
        for (const char* p=curl_error_buffer_; p && *p && connect_diagnostics_.detail.size()<180; ++p) {
            const unsigned char c=static_cast<unsigned char>(*p);
            connect_diagnostics_.detail.push_back(c>=0x20 && c<0x7f ? static_cast<char>(c) : ' ');
        }
        last_error_ = "Perform Failed: " + std::string(curl_easy_strerror(res));
        brls::Logger::error("WebSocket connect failed: {}", curl_easy_strerror(res));
        close_transport();
        return false;
    }

    // Generate random key
    std::vector<uint8_t> key = random_bytes(16);
    std::string key_b64 = base64_encode(key.data(), key.size());
    const auto expected_accept = opennow::websocket::AcceptForKey(key_b64);
    if (!expected_accept) {
        connect_diagnostics_.stage=Stage::InitFailed;
        last_error_ = "Could not calculate WebSocket challenge";
        close_transport();
        return false;
    }

    std::string handshake = "GET " + endpoint->request_target + " HTTP/1.1\r\n"
                            "Host: " + endpoint->host_header + "\r\n"
                            "Upgrade: websocket\r\n"
                            "Connection: Upgrade\r\n"
                            "Sec-WebSocket-Key: " + key_b64 + "\r\n"
                            "Sec-WebSocket-Version: 13\r\n";
    for (const auto& h : custom_headers_) {
        handshake += h + "\r\n";
    }
    handshake += "\r\n";

    if (!send_handshake(reinterpret_cast<const uint8_t*>(handshake.data()), handshake.size())) {
        connect_diagnostics_.stage=cancelled && cancelled->load() ? Stage::Cancelled : Stage::HandshakeSendFailed;
        close_transport();
        return false;
    }

    // Receive handshake response
    char buf[1024];
    size_t rcvd = 0;
    std::string response;
    
    int retries = 50; // 5 seconds max
    while (retries > 0 && std::chrono::steady_clock::now()<connect_deadline_) {
        if (cancelled && cancelled->load()) {
            connect_diagnostics_.stage=Stage::Cancelled;last_error_="WebSocket connect cancelled";break;
        }
        res = curl_easy_recv(curl_, buf, sizeof(buf) - 1, &rcvd);
        if (res == CURLE_OK && rcvd > 0) {
            response.append(buf, rcvd);
            if (response.size() > kMaximumHandshakeBytes) {
                connect_diagnostics_.stage=Stage::UpgradeTooLarge;
                last_error_ = "WebSocket handshake response is too large";
                break;
            }
            const size_t header_end = response.find("\r\n\r\n");
            if (header_end == std::string::npos)
                continue;
            connect_diagnostics_.http_status=opennow::websocket::UpgradeHttpStatus(response);

            if (opennow::websocket::ValidateUpgrade(response, *expected_accept)) {
                const size_t body_start = header_end + 4;
                if (response.size() > body_start) {
                    rx_buffer_.insert(rx_buffer_.end(), response.begin() + body_start, response.end());
                }
                connected_ = true;
                connect_diagnostics_.stage=Stage::Ready;
                brls::Logger::info("WebSocket connected!");
                return true;
            } else {
                connect_diagnostics_.stage=connect_diagnostics_.http_status!=0 && connect_diagnostics_.http_status!=101
                    ? Stage::UpgradeHttpRejected : Stage::UpgradeInvalid;
                last_error_ = "Invalid WebSocket upgrade response";
                brls::Logger::error("Invalid WebSocket upgrade response");
                break;
            }
        } else if (res == CURLE_AGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            retries--;
        } else {
            connect_diagnostics_.stage=Stage::HandshakeReadFailed;
            connect_diagnostics_.curl_code=static_cast<int>(res);
            last_error_ = "Recv error: " + std::string(curl_easy_strerror(res));
            brls::Logger::error("WebSocket recv error: {}", curl_easy_strerror(res));
            break;
        }
    }
    if (last_error_.empty()) {
        connect_diagnostics_.stage=Stage::HandshakeTimeout;last_error_ = "Handshake timeout";
    }
    close_transport();
    return false;
}

void WebSocketClient::reset_endpoint(const std::string& url) {
    disconnect();close_transport();url_=url;
}

void WebSocketClient::disconnect() {
    if (connected_) {
        uint8_t close_payload[] = { 0x03, 0xe8 }; // Normal closure
        if (!send_frame(0x88, close_payload, sizeof(close_payload)))
            return;
        connected_ = false;
        closing_ = true;
    }
    if (closing_)
        drain_outgoing();
}

void WebSocketClient::close_transport() {
    connected_ = false;
    closing_ = false;
    tx_queue_.reset();
    rx_buffer_.clear();
    fragmented_message_.clear();
    fragmented_opcode_ = 0;
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
}

bool WebSocketClient::drain_outgoing() {
    using Queue = opennow::websocket::WriteQueue;
    if (!curl_)
        return false;
    CURLcode error = CURLE_OK;
    const auto result = tx_queue_.drain([&](const uint8_t* data, size_t length) {
        size_t sent = 0;
        error = curl_easy_send(curl_, data, length, &sent);
        if (error == CURLE_AGAIN)
            return Queue::WriteResult{Queue::WriteStatus::Again, 0};
        return Queue::WriteResult{
            error == CURLE_OK ? Queue::WriteStatus::Progress : Queue::WriteStatus::Error, sent};
    }, [] { return Queue::Clock::now(); });

    if (result == Queue::DrainResult::Complete || result == Queue::DrainResult::Pending) {
        if (closing_ && result == Queue::DrainResult::Complete)
            close_transport();
        return true;
    }
    if (result == Queue::DrainResult::TimedOut)
        last_error_ = "Send timeout";
    else if (result == Queue::DrainResult::Stalled)
        last_error_ = "Send stalled";
    else
        last_error_ = "Send error: " + std::string(curl_easy_strerror(error));
    close_transport();
    return false;
}

bool WebSocketClient::send_handshake(const uint8_t* data, size_t length) {
    if (!curl_) return false;

    size_t total_sent = 0;
    const auto deadline = std::min(connect_deadline_,std::chrono::steady_clock::now() + opennow::websocket::WriteQueue::SendTimeout);
    while (total_sent < length) {
        if (connect_cancelled_ && connect_cancelled_->load()) {
            last_error_="WebSocket connect cancelled";return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            last_error_ = "Send timeout";
            return false;
        }
        size_t sent = 0;
        CURLcode res = curl_easy_send(curl_, data + total_sent, length - total_sent, &sent);
        if (res == CURLE_AGAIN) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        if (res != CURLE_OK) {
            connect_diagnostics_.curl_code=static_cast<int>(res);
            last_error_ = "Send error: " + std::string(curl_easy_strerror(res));
            connected_ = false;
            return false;
        }
        if (sent == 0) {
            last_error_ = "Send stalled";
            connected_ = false;
            return false;
        }

        total_sent += sent;
    }

    return true;
}

bool WebSocketClient::send_frame(uint8_t opcode, const uint8_t* payload, size_t length) {
    if (!connected_ || !curl_) return false;

    const size_t header_size = length < 126 ? 6 : (length <= 0xFFFF ? 8 : 14);
    if (length > kMaximumSignalingPayloadBytes || !tx_queue_.can_enqueue(length + header_size)) {
        last_error_ = "Outgoing WebSocket queue is full";
        close_transport();
        return false;
    }
    std::vector<uint8_t> frame;
    frame.reserve(length + header_size);
    frame.push_back(opcode | 0x80);

    // Client must mask frames
    uint8_t mask_bit = 0x80;
    if (length < 126) {
        frame.push_back(mask_bit | (uint8_t)length);
    } else if (length <= 0xFFFF) {
        frame.push_back(mask_bit | 126);
        frame.push_back((length >> 8) & 0xFF);
        frame.push_back(length & 0xFF);
    } else {
        frame.push_back(mask_bit | 127);
        for (int i = 7; i >= 0; i--) {
            frame.push_back((length >> (i * 8)) & 0xFF);
        }
    }

    std::vector<uint8_t> mask_key = random_bytes(4);
    for (uint8_t byte : mask_key) frame.push_back(byte);

    for (size_t i = 0; i < length; i++) {
        frame.push_back(payload[i] ^ mask_key[i % 4]);
    }

    return tx_queue_.enqueue(std::move(frame), opennow::websocket::WriteQueue::Clock::now());
}

void WebSocketClient::send_message(const std::string& msg) {
    send_frame(0x81, (const uint8_t*)msg.data(), msg.length());
}

bool WebSocketClient::send_binary(const std::vector<uint8_t>& msg) {
    return send_frame(0x82, msg.data(), msg.size());
}

void WebSocketClient::poll() {
    if ((!connected_ && !closing_) || !curl_) return;
    if (closing_) {
        drain_outgoing();
        return;
    }

    // Process all complete frames in the buffer
    size_t frames_processed = 0;
    while (rx_buffer_.size() >= 2 && frames_processed < kMaximumFramesPerPoll) {
        uint8_t opcode = rx_buffer_[0] & 0x0F;
        const bool final = (rx_buffer_[0] & 0x80) != 0;
        const bool control = (opcode & 0x08) != 0;
        uint8_t payload_len_7 = rx_buffer_[1] & 0x7F;
        bool masked = (rx_buffer_[1] & 0x80) != 0;

        if ((rx_buffer_[0] & 0x70) != 0 || masked ||
            (opcode != 0 && opcode != 1 && opcode != 2 &&
             opcode != 8 && opcode != 9 && opcode != 10) ||
            (control && (!final || payload_len_7 > 125)) ||
            (!control && ((opcode == 0) != (fragmented_opcode_ != 0)))) {
            last_error_ = "Invalid incoming WebSocket frame";
            close_transport();
            return;
        }
        
        size_t header_size = 2;
        size_t payload_len = payload_len_7;
        
        if (payload_len_7 == 126) {
            header_size += 2;
            if (rx_buffer_.size() < header_size) break;
            payload_len = (static_cast<size_t>(rx_buffer_[2]) << 8) | rx_buffer_[3];
        } else if (payload_len_7 == 127) {
            header_size += 8;
            if (rx_buffer_.size() < header_size) break;
            uint64_t big_len = 0;
            for (size_t i = 0; i < 8; ++i) {
                big_len = (big_len << 8) | rx_buffer_[2 + i];
            }
            if (big_len > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
                last_error_ = "Incoming WebSocket frame is too large";
                close_transport();
                return;
            }
            payload_len = static_cast<size_t>(big_len);
        }

        if (payload_len > kMaximumSignalingPayloadBytes ||
            payload_len > std::numeric_limits<size_t>::max() - header_size ||
            (!control && payload_len > kMaximumSignalingPayloadBytes - fragmented_message_.size())) {
            last_error_ = "Incoming WebSocket frame is too large";
            close_transport();
            return;
        }
        
        if (rx_buffer_.size() < header_size + payload_len) {
            break;
        }
        
        std::vector<uint8_t> payload(payload_len);
        if (payload_len > 0)
            std::memcpy(payload.data(), rx_buffer_.data() + header_size, payload_len);
        
        // Remove parsed frame from buffer
        rx_buffer_.erase(rx_buffer_.begin(), rx_buffer_.begin() + header_size + payload_len);
        frames_processed++;
        
        // Handle frame
        if (!control) {
            if (opcode != 0)
                fragmented_opcode_ = opcode;
            fragmented_message_.append(payload.begin(), payload.end());
            if (final) {
                const bool text = fragmented_opcode_ == 1;
                std::string message = std::move(fragmented_message_);
                fragmented_message_.clear();
                fragmented_opcode_ = 0;
                if (text && on_message_)
                    on_message_(message);
                else if (!text && on_binary_message_)
                    on_binary_message_(std::vector<uint8_t>(message.begin(), message.end()));
            }
        } else if (opcode == 0x08) { // Close frame
            uint16_t close_code = 0;
            if (payload.size() >= 2)
                close_code = static_cast<uint16_t>((payload[0] << 8) | payload[1]);
            last_error_ = "Signaling closed";
            if (close_code != 0)
                last_error_ += " code=" + std::to_string(close_code);
            if (!send_frame(0x88, payload.data(), payload.size()))
                return;
            connected_ = false;
            closing_ = true;
        } else if (opcode == 0x09) { // Ping frame
            send_frame(0x8A, payload.data(), payload.size()); // Send Pong
        }
        if (!connected_)
            break;
    }

    if (!drain_outgoing() || !connected_ || frames_processed >= kMaximumFramesPerPoll)
        return;

    uint8_t temp[4096];
    size_t read_this_poll = 0;
    while (read_this_poll < kMaximumPollReadBytes) {
        size_t rcvd = 0;
        CURLcode res = curl_easy_recv(curl_, temp, sizeof(temp), &rcvd);
        if (res == CURLE_OK && rcvd > 0) {
            if (rx_buffer_.size() > kMaximumSignalingPayloadBytes + 14 - rcvd) {
                last_error_ = "Incoming WebSocket buffer is too large";
                close_transport();
                return;
            }
            rx_buffer_.insert(rx_buffer_.end(), temp, temp + rcvd);
            read_this_poll += rcvd;
        } else {
            if (res == CURLE_OK && rcvd == 0) {
                if (read_this_poll > 0)
                    break;
                last_error_ = "Remote endpoint closed the signaling connection";
                close_transport();
            } else if (res != CURLE_AGAIN) {
                last_error_ = "Receive error: " + std::string(curl_easy_strerror(res));
                close_transport();
            }
            break;
        }
    }
}
