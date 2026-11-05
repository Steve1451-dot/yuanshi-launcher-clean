#include "WebSocketClient.hpp"
#include "websocket_handshake.hpp"
#include "mihoyo/signaling_client.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdarg>
#include <deque>
#include <memory>
#include <thread>

#ifdef CLOUDGENSHIN_TEST_HTTP_TLS_SHIM
// The Windows host test replaces curl with a fake transport and links only the
// WebSocket unit. Production TLS configuration is still used in Switch builds.
namespace opennow {
bool ConfigureHttpTls(CURL*) noexcept { return true; }
}
extern "C" unsigned char mbedtls_ct_base64_enc_char(unsigned char value) {
    if (value < 26) return static_cast<unsigned char>('A' + value);
    if (value < 52) return static_cast<unsigned char>('a' + value - 26);
    if (value < 62) return static_cast<unsigned char>('0' + value - 52);
    return value == 62 ? '+' : '/';
}
extern "C" signed char mbedtls_ct_base64_dec_value(unsigned char value) {
    if (value >= 'A' && value <= 'Z') return value - 'A';
    if (value >= 'a' && value <= 'z') return value - 'a' + 26;
    if (value >= '0' && value <= '9') return value - '0' + 52;
    if (value == '+') return 62;
    if (value == '/') return 63;
    return -1;
}
#endif

namespace
{

enum class UpgradeResponse { Valid, Bare, WrongAccept, Forbidden, Unavailable };
UpgradeResponse next_upgrade = UpgradeResponse::Valid;
CURLcode next_perform=CURLE_OK;
bool fake_rtt_echo=false;
struct FakeTiming {
    curl_off_t lookup=100,connect=10000,tls=30000,ready=31000,total=31000;
    long os_error=0;
};
FakeTiming next_timing{};

struct FakeTransport
{
    struct Write
    {
        CURLcode result;
        size_t bytes;
    };

    bool handshaking = true;
    bool closed = false;
    size_t calls = 0;
    std::deque<Write> writes;
    std::vector<uint8_t> output;
    std::vector<uint8_t> input;
    CURLcode receive_result = CURLE_AGAIN;
    UpgradeResponse upgrade = std::exchange(next_upgrade, UpgradeResponse::Valid);
    CURLcode perform=std::exchange(next_perform,CURLE_OK);
    FakeTiming timing=std::exchange(next_timing,FakeTiming{});
    std::string request;
    std::size_t echoed_frames=0;
};

std::vector<std::unique_ptr<FakeTransport>> transports;

FakeTransport& transport(CURL* curl)
{
    auto& result = *reinterpret_cast<FakeTransport*>(curl);
    assert(!result.closed);
    return result;
}

FakeTransport& latest()
{
    return *transports.back();
}

struct Frame
{
    uint8_t opcode;
    std::string payload;
};

std::vector<Frame> frames(const FakeTransport& fake)
{
    std::vector<Frame> result;
    size_t offset = 0;
    while (offset < fake.output.size()) {
        const auto& bytes = fake.output;
        assert(bytes.size() - offset >= 6);
        const uint8_t opcode = bytes[offset++];
        assert(opcode & 0x80);
        assert(bytes[offset] & 0x80);
        size_t length = bytes[offset++] & 0x7f;
        if (length == 126 || length == 127) {
            const size_t length_bytes = length == 126 ? 2 : 8;
            assert(bytes.size() - offset >= length_bytes + 4);
            length = 0;
            for (size_t i = 0; i < length_bytes; ++i)
                length = (length << 8) | bytes[offset++];
        }
        const size_t mask = offset;
        offset += 4;
        assert(bytes.size() - offset >= length);
        std::string payload;
        for (size_t i = 0; i < length; ++i)
            payload.push_back(bytes[offset++] ^ bytes[mask + i % 4]);
        result.push_back({static_cast<uint8_t>(opcode & 0x0f), std::move(payload)});
    }
    return result;
}

}

extern "C" {

CURL* curl_easy_init()
{
    transports.push_back(std::make_unique<FakeTransport>());
    return reinterpret_cast<CURL*>(transports.back().get());
}

void curl_easy_cleanup(CURL* curl)
{
    transport(curl).closed = true;
}

CURLcode curl_easy_setopt(CURL*, CURLoption, ...)
{
    return CURLE_OK;
}

CURLcode curl_easy_perform(CURL* curl)
{
    return transport(curl).perform;
}

CURLcode curl_easy_getinfo(CURL* curl,CURLINFO info,...)
{
    const auto& timing=transport(curl).timing;
    va_list args;va_start(args,info);
    if(info==CURLINFO_OS_ERRNO) *va_arg(args,long*)=timing.os_error;
    else {
        curl_off_t value=0;
        if(info==CURLINFO_NAMELOOKUP_TIME_T) value=timing.lookup;
        else if(info==CURLINFO_CONNECT_TIME_T) value=timing.connect;
        else if(info==CURLINFO_APPCONNECT_TIME_T) value=timing.tls;
        else if(info==CURLINFO_PRETRANSFER_TIME_T) value=timing.ready;
        else if(info==CURLINFO_TOTAL_TIME_T) value=timing.total;
        else assert(info==CURLINFO_STARTTRANSFER_TIME_T || info==CURLINFO_SIZE_UPLOAD_T);
        *va_arg(args,curl_off_t*)=value;
    }
    va_end(args);return CURLE_OK;
}

const char* curl_easy_strerror(CURLcode)
{
    return "fake transport error";
}

CURLcode curl_easy_send(CURL* curl, const void* buffer, size_t length, size_t* sent)
{
    auto& fake = transport(curl);
    if (fake.handshaking) {
        fake.request.append(static_cast<const char*>(buffer), length);
        *sent = length;
        return CURLE_OK;
    }
    ++fake.calls;
    assert(length <= opennow::websocket::WriteQueue::MaximumWriteBytes);
    FakeTransport::Write write{CURLE_OK, length};
    if (!fake.writes.empty()) {
        write = fake.writes.front();
        fake.writes.pop_front();
    }
    *sent = write.result == CURLE_OK ? std::min(length, write.bytes) : 0;
    const auto* bytes = static_cast<const uint8_t*>(buffer);
    fake.output.insert(fake.output.end(), bytes, bytes + *sent);
    if(fake_rtt_echo && write.result==CURLE_OK && *sent==length) {
        const auto all=frames(fake);
        while(fake.echoed_frames<all.size()) {
            const auto& frame=all[fake.echoed_frames++];
            if(frame.opcode==8) continue; // Normal client shutdown is not a probe.
            assert(frame.opcode==1 && frame.payload=="X");
            fake.input.insert(fake.input.end(),{0x81,1,'X'});
        }
    }
    return write.result;
}

CURLcode curl_easy_recv(CURL* curl, void* buffer, size_t length, size_t* received)
{
    auto& fake = transport(curl);
    if (fake.handshaking) {
        const std::string key_header = "Sec-WebSocket-Key: ";
        const size_t start = fake.request.find(key_header);
        assert(start != std::string::npos);
        const size_t value = start + key_header.size();
        const auto accept = opennow::websocket::AcceptForKey(
            fake.request.substr(value, fake.request.find("\r\n", value) - value));
        assert(accept);
        const std::string response = fake.upgrade==UpgradeResponse::Forbidden
            ? "HTTP/1.1 403 Forbidden\r\nContent-Length: 16\r\n\r\nsynthetic-secret"
            : fake.upgrade==UpgradeResponse::Unavailable
            ? "HTTP/1.1 503 Unavailable\r\nContent-Length: 0\r\n\r\n"
            : "HTTP/1.1 101 Switching Protocols\r\n" +
            (fake.upgrade == UpgradeResponse::Bare ? std::string() :
                "Upgrade: WebSocket\r\nConnection: keep-alive, Upgrade\r\nSec-WebSocket-Accept: " +
                (fake.upgrade == UpgradeResponse::WrongAccept ? "synthetic-secret" : *accept) + "\r\n") + "\r\n";
        assert(length >= response.size());
        std::memcpy(buffer, response.data(), response.size());
        *received = response.size();
        fake.handshaking = false;
        return CURLE_OK;
    }
    *received = std::min(length, fake.input.size());
    if (*received == 0)
        return fake.receive_result;
    std::memcpy(buffer, fake.input.data(), *received);
    fake.input.erase(fake.input.begin(), fake.input.begin() + *received);
    return CURLE_OK;
}

}

int main()
{
    using Queue = opennow::websocket::WriteQueue;
    for (const auto base : {"wss://example.invalid?x=synthetic-secret",
                           "wss://example.invalid/rtc-sdk?x=synthetic-secret"}) {
        cloudgenshin::SignalingClient client(base);
        assert(client.connect(false));
        assert(latest().request.starts_with("GET /rtc-sdk?x=synthetic-secret HTTP/1.1\r\n"));
        assert(latest().request.find("Host: example.invalid\r\n")!=std::string::npos);
    }
    {
        WebSocketClient client("wss://example.invalid?sessionId=synthetic-secret");
        assert(client.connect());
        assert(latest().request.starts_with("GET /?sessionId=synthetic-secret HTTP/1.1\r\n"));
        assert(latest().request.find("Host: example.invalid\r\n")!=std::string::npos);
        assert(client.connect_diagnostics().http_status==101);
        assert(std::string(client.connect_diagnostics().event())=="SIGNALING_UPGRADE_OK");
        const auto& timing=client.connect_diagnostics().transfer;
        assert(timing.captured && timing.requires_tls && timing.lookup_ms==1 && timing.connect_ms==10);
        assert(timing.tls_ms==30 && timing.ready_ms==31 && timing.total_ms==31);
    }
    for(const auto phase:{opennow::HttpFailurePhase::Resolving,opennow::HttpFailurePhase::Connecting,
                         opennow::HttpFailurePhase::TlsHandshake}) {
        next_perform=CURLE_OPERATION_TIMEDOUT;
        next_timing={phase==opennow::HttpFailurePhase::Resolving ? 0 : 100,
                     phase==opennow::HttpFailurePhase::TlsHandshake ? 10000 : 0,0,0,2500000,110};
        WebSocketClient client("wss://example.invalid:8443/echo?key=synthetic-secret");
        assert(!client.connect());
        const auto& diagnostic=client.connect_diagnostics();
        assert(diagnostic.transfer.phase(0)==phase && diagnostic.transfer.total_ms==2500);
        assert(diagnostic.os_error==110 && diagnostic.endpoint_port==8443);
        assert(diagnostic.endpoint_path.find("synthetic-secret")==std::string::npos);
    }
    for (const auto error : {CURLE_COULDNT_RESOLVE_HOST,CURLE_COULDNT_CONNECT,CURLE_PEER_FAILED_VERIFICATION}) {
        next_perform=error;
        WebSocketClient client("wss://example.invalid/path?sessionId=synthetic-secret");
        assert(!client.connect());
        assert(client.connect_diagnostics().curl_code==static_cast<int>(error));
        assert(std::string(client.connect_diagnostics().event())=="SIGNALING_TCP_TLS_FAILED");
        assert(client.get_last_error().find("synthetic-secret")==std::string::npos);
    }
    {
        next_upgrade=UpgradeResponse::Forbidden;
        WebSocketClient client("wss://example.invalid/cloud");
        assert(!client.connect());
        assert(client.connect_diagnostics().http_status==403);
        assert(!client.connect_diagnostics().may_try_alternative());
        assert(client.get_last_error().find("synthetic-secret")==std::string::npos);
    }
    {
        next_perform=CURLE_COULDNT_CONNECT;
        cloudgenshin::SignalingClient client("wss://example.invalid/primary",{"wss://example.invalid/backup"});
        std::vector<opennow::websocket::ConnectDiagnostics> attempts;
        client.set_on_connect_attempt([&](unsigned index,const auto& diagnostic) {
            assert(index==attempts.size()+1);attempts.push_back(diagnostic);
        });
        assert(client.connect(false) && attempts.size()==2);
        assert(attempts[0].curl_code==CURLE_COULDNT_CONNECT && attempts[1].http_status==101);
        assert(latest().request.find("Origin: https://ys.mihoyo.com\r\n")!=std::string::npos);
        assert(latest().request.starts_with("GET /backup/rtc-sdk HTTP/1.1\r\n"));
        assert(latest().request.find("Cookie:")==std::string::npos);
    }
    for (const auto upgrade : {UpgradeResponse::Forbidden,UpgradeResponse::WrongAccept}) {
        next_upgrade=upgrade;
        cloudgenshin::SignalingClient client("wss://example.invalid/primary",{"wss://example.invalid/backup"});
        unsigned attempts=0;
        client.set_on_connect_attempt([&](unsigned,const auto&){++attempts;});
        assert(!client.connect(false) && attempts==1);
    }
    {
        next_upgrade=UpgradeResponse::Unavailable;
        cloudgenshin::SignalingClient client("wss://example.invalid/primary",{"wss://example.invalid/backup"});
        unsigned attempts=0;
        client.set_on_connect_attempt([&](unsigned,const auto&){++attempts;});
        assert(client.connect(false) && attempts==2);
    }
    {
        cloudgenshin::SignalingClient client("ws://example.invalid/rtc-sdk");
        assert(!client.connect());
        assert(client.last_error() == "Cloud signaling requires a secure WSS endpoint");
    }
    {
        using namespace cloudgenshin::signaling;
        cloudgenshin::SignalingClient client("wss://example.invalid/rtc-sdk");
        assert(client.connect(false));
        auto& fake=latest();
        client.poll();
        assert(frames(fake).empty());
        assert(client.send(MessageType::Keepalive,{}));
        client.poll();
        assert(frames(fake).size()==1 && frames(fake)[0].opcode==1 && frames(fake)[0].payload.empty());
        fake.input={0x81,0};client.poll();assert(client.is_connected());
        assert(client.send_client_hello());client.poll();
        assert(frames(fake).size()==2 && frames(fake)[1].opcode==2);
    }
    {
        using namespace cloudgenshin::signaling;
        cloudgenshin::SignalingClient client("wss://example.invalid/rtc-sdk");
        assert(client.connect());
        auto& fake = latest();
        std::vector<cloudgenshin::signaling::Frame> received;
        std::vector<cloudgenshin::rtc::SignalMessage> rtc_received;
        client.set_on_frame([&](const cloudgenshin::signaling::Frame& frame) {
            received.push_back(frame);
        });
        client.set_on_rtc_signal([&](const cloudgenshin::rtc::SignalMessage& signal) {
            rtc_received.push_back(signal);
        });
        const auto wire = EncodeFrame(MessageType::Handshake, {'o', 'k'});
        fake.input = {0x82, static_cast<uint8_t>(wire.size())};
        fake.input.insert(fake.input.end(), wire.begin(), wire.end());
        client.poll();
        client.poll();
        assert(received.size() == 1);
        assert(received[0].type == MessageType::Handshake);
        assert(received[0].payload == std::vector<uint8_t>({'o', 'k'}));
        const std::string offer_json = R"({"type":"offer","sdp":"v=0\r\n"})";
        const auto offer_wire = EncodeFrame(MessageType::Signaling,
                                            {offer_json.begin(), offer_json.end()});
        assert(offer_wire.size() < 126);
        fake.input = {0x82, static_cast<uint8_t>(offer_wire.size())};
        fake.input.insert(fake.input.end(), offer_wire.begin(), offer_wire.end());
        client.poll();
        client.poll();
        assert(rtc_received.size() == 1 && rtc_received[0].type == cloudgenshin::rtc::SignalType::Offer);
        assert(rtc_received[0].sdp == "v=0\r\n");
        assert(client.send(MessageType::Signaling, {'a', 0, 'b'}));
        assert(client.send_answer("v=0\r\n"));
        assert(client.send_candidate("candidate:1 1 UDP 1 192.0.2.2 9 typ host", "0", 0));
        client.poll();
        const auto sent = frames(fake);
        const auto expected_wire = EncodeFrame(MessageType::Signaling, {'a', 0, 'b'});
        assert(sent.size() == 4 && sent[0].opcode == 2);
        const auto hello_frame = DecodeFrame({sent[0].payload.begin(), sent[0].payload.end()});
        assert(hello_frame && hello_frame->type == MessageType::Handshake);
        assert(std::string(hello_frame->payload.begin(), hello_frame->payload.end()) ==
               R"({"client_type":"web","type":"client hello"})");
        assert(sent[1].payload == std::string(expected_wire.begin(), expected_wire.end()));
        const auto answer_frame = DecodeFrame({sent[2].payload.begin(), sent[2].payload.end()});
        assert(answer_frame && answer_frame->type == MessageType::Signaling);
        const auto answer_signal = cloudgenshin::rtc::ParseSignalMessage(answer_frame->payload);
        assert(answer_signal && answer_signal->type == cloudgenshin::rtc::SignalType::Answer);
        const auto candidate_frame = DecodeFrame({sent[3].payload.begin(), sent[3].payload.end()});
        assert(candidate_frame && candidate_frame->type == MessageType::Signaling);
        const auto candidate_signal = cloudgenshin::rtc::ParseSignalMessage(candidate_frame->payload);
        assert(candidate_signal && candidate_signal->type == cloudgenshin::rtc::SignalType::Candidate);

        fake.input = {0x82, 8, 0, 0, 0, 99, 0, 0, 0, 0};
        client.poll();
        client.poll();
        assert(!client.is_connected());
        assert(client.last_error() == "Invalid cloud signaling frame");
    }
    for (const auto response : {UpgradeResponse::Bare, UpgradeResponse::WrongAccept}) {
        next_upgrade = response;
        WebSocketClient client("wss://example.invalid");
        assert(!client.connect());
        assert(latest().closed);
        assert(client.get_last_error() == "Invalid WebSocket upgrade response");
    }
    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        std::vector<std::string> messages;
        client.set_on_message([&](const std::string& message) { messages.push_back(message); });
        fake.input = {0x01, 3, '{', '"', 'h', 0x89, 1, 'p'};
        client.poll();
        client.poll();
        assert(messages.empty());
        assert(frames(fake).size() == 1 && frames(fake)[0].opcode == 0x0a);
        fake.input = {0x00, 3, 'b', '"', ':', 0x80, 2, '1', '}'};
        client.poll();
        client.poll();
        assert(messages == std::vector<std::string>{"{\"hb\":1}"});
    }

    for (const std::vector<uint8_t>& input : std::vector<std::vector<uint8_t>>{
             {0x80, 0}, {0x01, 0, 0x81, 0}, {0x09, 0}, {0x89, 126},
             {0x81, 0x80}, {0xc1, 0}, {0x83, 0}}) {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        fake.input = input;
        client.poll();
        client.poll();
        assert(!client.is_connected());
        assert(fake.closed);
        assert(client.get_last_error() == "Invalid incoming WebSocket frame");
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        std::vector<std::string> messages;
        client.set_on_message([&](const std::string& message) { messages.push_back(message); });
        fake.input = {0x02, 1, 'b', 0x80, 1, 'i', 0x01, 0};
        for (size_t i = 0; i < 20; ++i)
            fake.input.insert(fake.input.end(), {0x00, 1, 'x'});
        fake.input.insert(fake.input.end(), {0x80, 0});
        client.poll();
        client.poll();
        assert(messages.empty());
        client.poll();
        assert(messages == std::vector<std::string>{std::string(20, 'x')});
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        std::vector<std::vector<uint8_t>> messages;
        client.set_on_binary_message([&](const std::vector<uint8_t>& message) {
            messages.push_back(message);
        });
        fake.input = {0x02, 2, 0, 0x45, 0x80, 2, 0x67, 0};
        client.poll();
        client.poll();
        const std::vector<std::vector<uint8_t>> expected{{0, 0x45, 0x67, 0}};
        assert(messages == expected);

        assert(client.send_binary({0, 0x45, 0x67, 0}));
        client.poll();
        const auto sent = frames(fake);
        assert(sent.size() == 1);
        assert(sent[0].opcode == 2);
        assert(sent[0].payload == std::string("\0\x45\x67\0", 4));
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        std::vector<std::string> messages;
        client.set_on_message([&](const std::string& message) { messages.push_back(message); });
        auto& old = latest();
        old.input = {0x01, 1, 'x'};
        client.poll();
        client.poll();
        assert(messages.empty());
        assert(client.connect());
        auto& fresh = latest();
        fresh.input = {0x81, 1, 'y', 0x88, 2, 0x03, 0xe8};
        fresh.receive_result = CURLE_OK;
        client.poll();
        client.poll();
        assert(messages == std::vector<std::string>{"y"});
        assert(client.get_last_error() == "Signaling closed code=1000");
        assert(fresh.closed);
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        fake.input = {0x01, 127, 0, 0, 0, 0, 0, 0x40, 0, 0};
        fake.input.resize(fake.input.size() + 4 * 1024 * 1024, 'x');
        for (size_t i = 0; i < 66; ++i)
            client.poll();
        assert(client.is_connected());
        fake.input = {0x80, 1, 'x'};
        client.poll();
        client.poll();
        assert(!client.is_connected());
        assert(fake.closed);
        assert(client.get_last_error() == "Incoming WebSocket frame is too large");
    }

    {
        WebSocketClient client("wss://example.invalid/signaling");
        assert(client.connect());
        auto& fake = latest();
        std::vector<std::string> messages{"first", "second", "", std::string(125, 'a'),
                                          std::string(126, 'b'), std::string(65535, 'c'),
                                          std::string(70000, 'd')};
        for (const auto& message : messages)
            client.send_message(message);
        assert(fake.calls == 0);
        for (size_t i = 0; i < 4; ++i) {
            const auto calls = fake.calls;
            client.poll();
            assert(fake.calls - calls <= Queue::MaximumWritesPerPoll);
        }
        const auto sent = frames(fake);
        assert(sent.size() == messages.size());
        for (size_t i = 0; i < messages.size(); ++i) {
            assert(sent[i].opcode == 1);
            assert(sent[i].payload == messages[i]);
        }
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        client.send_message("first");
        client.send_message("second");
        fake.writes = {{CURLE_OK, 3}, {CURLE_AGAIN, 0}};
        fake.input = {0x89, 2, 'h', 'i'};
        client.poll();
        assert(fake.calls == 2);
        assert(fake.output.size() == 3);
        fake.writes = {{CURLE_AGAIN, 0}};
        client.poll();
        assert(fake.calls == 3);
        assert(fake.output.size() == 3);
        client.poll();
        const auto sent = frames(fake);
        assert(sent.size() == 3);
        assert(sent[0].payload == "first");
        assert(sent[1].payload == "second");
        assert(sent[2].opcode == 0x0a && sent[2].payload == "hi");
    }

    for (const bool remote_close : {false, true}) {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        size_t messages = 0;
        client.set_on_message([&](const std::string&) { ++messages; });
        client.send_message("pending");
        fake.writes = {{CURLE_OK, 3}, {CURLE_AGAIN, 0}};
        if (remote_close)
            fake.input = {0x88, 2, 0x03, 0xe8, 0x81, 1, 'x'};
        client.poll();
        fake.writes = {{CURLE_AGAIN, 0}};
        if (remote_close)
            client.poll();
        else
            client.disconnect();
        assert(!client.is_connected());
        assert(!fake.closed);
        assert(messages == 0);
        assert(fake.calls == 3);
        client.send_message("ignored");
        client.poll();
        assert(fake.closed);
        const auto sent = frames(fake);
        assert(sent.size() == 2);
        assert(sent[0].opcode == 1 && sent[0].payload == "pending");
        assert(sent[1].opcode == 8 && sent[1].payload == std::string("\x03\xe8", 2));
        if (remote_close)
            assert(client.get_last_error() == "Signaling closed code=1000");
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        client.set_on_message([&](const std::string& message) { client.send_message(message); });
        fake.input = {0x81, 1, 'x'};
        client.poll();
        client.poll();
        auto sent = frames(fake);
        assert(sent.size() == 1 && sent[0].payload == "x");
        fake.input = {0x88, 2, 0x03, 0xe8};
        client.poll();
        client.poll();
        assert(!client.is_connected());
        assert(fake.closed);
        sent = frames(fake);
        assert(sent.size() == 2 && sent[1].opcode == 8);
    }

    for (const bool byte_overflow : {false, true}) {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& old = latest();
        if (byte_overflow) {
            client.send_message(std::string(4 * 1024 * 1024, 'a'));
            old.writes = {{CURLE_OK, 3}, {CURLE_AGAIN, 0}};
            client.poll();
        } else {
            for (size_t i = 0; i < Queue::MaximumFrames; ++i)
                client.send_message("pending");
        }
        assert(client.is_connected());
        client.send_message("overflow");
        assert(!client.is_connected());
        assert(old.closed);
        assert(client.get_last_error() == "Outgoing WebSocket queue is full");
        const auto old_calls = old.calls;
        client.poll();
        assert(old.calls == old_calls);
        assert(client.connect());
        auto& fresh = latest();
        assert(&fresh != &old);
        assert(client.get_last_error().empty());
        client.send_message("fresh");
        client.poll();
        const auto sent = frames(fresh);
        assert(sent.size() == 1 && sent[0].payload == "fresh");
    }

    for (const CURLcode error : {CURLE_OK, CURLE_SEND_ERROR}) {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        client.send_message("pending");
        fake.writes = {{error, 0}};
        client.poll();
        assert(!client.is_connected());
        assert(fake.closed);
        assert(fake.calls == 1);
        assert(client.get_last_error() == (error == CURLE_OK ? "Send stalled" :
                                          "Send error: fake transport error"));
        client.poll();
        client.disconnect();
        assert(fake.calls == 1);
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        client.send_message(std::string(4 * 1024 * 1024 + 1, 'x'));
        assert(!client.is_connected());
        assert(fake.closed);
        assert(fake.calls == 0);
    }

    for (const CURLcode error : {CURLE_OK, CURLE_RECV_ERROR}) {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        client.send_message("pending");
        fake.writes = {{CURLE_OK, 3}, {CURLE_AGAIN, 0}};
        fake.receive_result = error;
        client.poll();
        assert(!client.is_connected());
        assert(fake.closed);
        client.poll();
        assert(fake.calls == 2);
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& fake = latest();
        client.send_message("pending");
        fake.writes = {{CURLE_OK, 3}, {CURLE_AGAIN, 0}};
        client.poll();
        // A host sleep can return early (e.g. interrupted MinGW nanosleep).
        // Check the same monotonic clock as WriteQueue rather than assuming
        // that one sleep call has advanced it through the send deadline.
        const auto timeout_at=Queue::Clock::now()+Queue::SendTimeout;
        while (Queue::Clock::now()<timeout_at) std::this_thread::sleep_until(timeout_at);
        client.poll();
        assert(!client.is_connected());
        assert(fake.closed);
        assert(fake.calls == 2);
        assert(client.get_last_error() == "Send timeout");
    }

    {
        WebSocketClient client("ws://example.invalid");
        assert(client.connect());
        auto& old = latest();
        client.send_message("pending");
        old.writes = {{CURLE_OK, 3}, {CURLE_AGAIN, 0}, {CURLE_AGAIN, 0}};
        client.disconnect();
        assert(client.connect());
        assert(old.closed);
        auto& fresh = latest();
        client.send_message("fresh");
        client.poll();
        const auto sent = frames(fresh);
        assert(sent.size() == 1 && sent[0].payload == "fresh");
    }

    for (const auto& fake : transports)
        assert(fake->closed);
    return 0;
}
