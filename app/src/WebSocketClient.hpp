#pragma once

#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include "websocket_write_queue.hpp"
#include "websocket_connect_policy.hpp"

class WebSocketClient {
public:
    using MessageCallback = std::function<void(const std::string&)>;
    using BinaryMessageCallback = std::function<void(const std::vector<uint8_t>&)>;

    WebSocketClient(const std::string& url);
    ~WebSocketClient();

    bool connect(const std::atomic_bool* cancelled = nullptr, long timeout_ms = 20000);
    void disconnect();
    void poll();
    void send_message(const std::string& msg);
    bool send_binary(const std::vector<uint8_t>& msg);

    void set_on_message(MessageCallback cb) { on_message_ = cb; }
    void set_on_binary_message(BinaryMessageCallback cb) { on_binary_message_ = cb; }
    void set_custom_headers(const std::vector<std::string>& headers) { custom_headers_ = headers; }
    bool is_connected() const { return connected_; }
    std::string get_last_error() const { return last_error_; }
    const opennow::websocket::ConnectDiagnostics& connect_diagnostics() const { return connect_diagnostics_; }
    void reset_endpoint(const std::string& url);

private:
    std::string url_;
    std::vector<std::string> custom_headers_;
    CURL* curl_ = nullptr;
    MessageCallback on_message_;
    BinaryMessageCallback on_binary_message_;
    bool connected_ = false;
    bool closing_ = false;
    const std::atomic_bool* connect_cancelled_ = nullptr;
    std::chrono::steady_clock::time_point connect_deadline_{};
    std::string last_error_;
    opennow::websocket::ConnectDiagnostics connect_diagnostics_;
    char curl_error_buffer_[CURL_ERROR_SIZE]{};
    std::vector<uint8_t> rx_buffer_;
    std::string fragmented_message_;
    uint8_t fragmented_opcode_ = 0;
    opennow::websocket::WriteQueue tx_queue_;

    void close_transport();
    bool drain_outgoing();
    bool send_handshake(const uint8_t* data, size_t length);
    bool send_frame(uint8_t opcode, const uint8_t* payload, size_t length);
};
