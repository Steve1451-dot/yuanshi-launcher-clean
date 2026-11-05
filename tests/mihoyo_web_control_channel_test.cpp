// Deterministic fake WebSocket transport; no account, capture or network.
#define main WebSocketTransportSelfTest
#include "websocket_client_test.cpp"
#undef main
#include "mihoyo/web_control_channel.hpp"
#include "mihoyo/game_control.hpp"

namespace {
void Incoming(FakeTransport& fake,const std::vector<std::uint8_t>& bytes) {
    assert(bytes.size()<65536);
    if(bytes.size()<126) fake.input.insert(fake.input.end(),{0x82,static_cast<std::uint8_t>(bytes.size())});
    else fake.input.insert(fake.input.end(),{0x82,126,static_cast<std::uint8_t>(bytes.size()>>8),static_cast<std::uint8_t>(bytes.size())});
    fake.input.insert(fake.input.end(),bytes.begin(),bytes.end());
}
cloudgenshin::control::Packet Outgoing(const FakeTransport& fake,std::size_t index,std::uint8_t prefix) {
    const auto all=frames(fake);assert(index<all.size());
    const auto& frame=all[index];assert(frame.opcode==2 && static_cast<std::uint8_t>(frame.payload.front())==prefix);
    const auto packet=cloudgenshin::control::DecodePacket({reinterpret_cast<const std::uint8_t*>(frame.payload.data()+1),frame.payload.size()-1});
    assert(packet);return *packet;
}
}
int main() {
    using namespace cloudgenshin;
    using namespace cloudgenshin::rtc;
    for(const auto* value:{"{}","[]","null","{\"session_id\":0}","{\"session_id\":-1}",
        "{\"session_id\":4294967296}","{\"session_id\":1.5}","{\"session_id\":\"42\"}"}) {
        const std::string text(value);assert(!ParseControlSession({reinterpret_cast<const std::uint8_t*>(text.data()),text.size()}));
    }
    const std::string json="{\"session_id\":4294967295}";
    assert(ParseControlSession({reinterpret_cast<const std::uint8_t*>(json.data()),json.size()})==UINT32_MAX);
    const std::vector<std::uint8_t> huge(8193,' ');assert(!ParseControlSession(huge));
    assert(WebControlEndpoint("https://example.invalid/gcc",42)=="wss://example.invalid/gcc?sessionId=42");
    assert(WebControlEndpoint("wss://example.invalid?x=1",42)=="wss://example.invalid?x=1&sessionId=42");
    assert(WebControlEndpoint("wss://[2001:db8::1]:443/gcc?",42)=="wss://[2001:db8::1]:443/gcc?sessionId=42");
    for(const auto* url:{"http://example.invalid/gcc","ws://example.invalid/gcc","wss://u@example.invalid/gcc",
        "wss://example.invalid/#fragment","wss://example.invalid/?sessionId=9","wss://example.invalid/?x=1&sessionId"})
        assert(WebControlEndpoint(url,42).empty());
    assert(WebControlEndpoint("https://example.invalid/gcc",0).empty());
    for(auto session:{1u,127u,128u,65535u,UINT32_MAX}) {
        for(std::uint16_t command=1250;command<=1254;++command) {
            const auto encoded=EncodeControlSync(command,session);
            assert(encoded.front()==0);
            const auto packet=control::DecodePacket(std::span(encoded).subspan(1));
            assert(packet && packet->command==command && DecodeControlSession(packet->body)==session);
        }
    }
    assert(EncodeControlSync(1249,42).empty() && EncodeControlSync(1250,0).empty());
    for(const auto& body:std::vector<std::vector<std::uint8_t>>{{},{8,0},{8,42,8,42},{8,128},
        {8,255,255,255,255,31},{8,128,128,128,128,128,128,128,128,128,2},{0,1},{10,1,42}})
        assert(!DecodeControlSession(body));
    std::vector<std::uint8_t> received;
    WebControlChannel channel([&](const std::uint8_t* data,std::size_t size){received.assign(data,data+size);});
    auto now=WebControlChannel::Clock::time_point{};
    const std::vector<std::string> endpoints{"https://example.invalid/gcc"};
    const auto incoming=[&](std::uint16_t command,std::uint32_t session=42) {
        Incoming(latest(),EncodeControlSync(command,session));channel.poll(now);channel.poll(now);
    };
    const auto game=control::EncodeHeartbeat(1,1000);
    assert(channel.start(endpoints,42,now,nullptr) && !channel.ready() && !channel.send(game));
    assert(latest().request.starts_with("GET /gcc?sessionId=42 HTTP/1.1\r\n"));
    assert(latest().request.find("Cookie:")==std::string::npos);
    channel.poll(now);assert(Outgoing(latest(),0,0).command==1250);
    std::vector<std::uint8_t> data{1};data.insert(data.end(),game.begin(),game.end());
    Incoming(latest(),data);channel.poll(now);channel.poll(now);assert(received.empty());
    incoming(1251);assert(channel.ready());
    assert(Outgoing(latest(),1,0).command==1252);
    assert(channel.send(game));channel.poll(now);assert(Outgoing(latest(),2,1).command==20029);
    Incoming(latest(),data);channel.poll(now);channel.poll(now);assert(received==game);
    now+=std::chrono::seconds(2);channel.poll(now);channel.poll(now);
    assert(Outgoing(latest(),3,0).command==1253);
    incoming(1254);
    for(int i=0;i<12;++i) {now+=std::chrono::seconds(2);incoming(1254);assert(channel.ready());}
    // A pong read on the timeout poll must be decoded before deciding the
    // channel is dead (WebSocketClient buffers newly read frames).
    now+=std::chrono::seconds(6);
    Incoming(latest(),EncodeControlSync(1254,42));channel.poll(now);
    assert(channel.ready() && !channel.error());
    now+=std::chrono::seconds(6);channel.poll(now);
    assert(!channel.ready() && std::string(channel.error())=="CONTROL_WSS_PONG_TIMEOUT");
    channel.stop();assert(!channel.ready() && !channel.error());
    assert(channel.start(endpoints,42,now,nullptr));incoming(1251,43);
    assert(std::string(channel.error())=="CONTROL_WSS_SESSION_MISMATCH");
    // The same buffered-read boundary applies to the initial sync reply.
    assert(channel.start(endpoints,42,now,nullptr));
    now+=std::chrono::seconds(15);
    Incoming(latest(),EncodeControlSync(1251,42));channel.poll(now);
    assert(channel.ready() && !channel.error());
    assert(channel.start(endpoints,42,now,nullptr));
    now+=std::chrono::seconds(15);channel.poll(now);
    assert(std::string(channel.error())=="CONTROL_WSS_SYNC_TIMEOUT");
    for(const auto& malformed:std::vector<std::vector<std::uint8_t>>{{},{2,0},{0,1,2},{1,1,2}}) {
        assert(channel.start(endpoints,42,now,nullptr));Incoming(latest(),malformed);
        channel.poll(now);channel.poll(now);
        assert(std::string(channel.error())=="CONTROL_WSS_PROTOCOL_INVALID");
    }
    assert(channel.start(endpoints,42,now,nullptr));incoming(1251);
    latest().receive_result=CURLE_RECV_ERROR;channel.poll(now);
    assert(std::string(channel.error())=="CONTROL_WSS_DISCONNECTED");
    assert(!channel.start({"http://example.invalid"},42,now,nullptr));
    assert(std::string(channel.error())=="CONTROL_WSS_ENDPOINT_MISSING");
    std::atomic_bool cancelled{true};
    const auto count=transports.size();
    assert(!channel.start(endpoints,42,now,&cancelled) && transports.size()==count);
    cancelled=false;
    next_perform=CURLE_PEER_FAILED_VERIFICATION;
    assert(!channel.start(endpoints,42,now,&cancelled));
    assert(std::string(channel.error())=="CONTROL_WSS_CONNECT_FAILED");
    assert(channel.start(endpoints,42,now,nullptr));incoming(1251);
    latest().receive_result=CURLE_RECV_ERROR;channel.poll(now);channel.stop();
    assert(!channel.send(game));
}
