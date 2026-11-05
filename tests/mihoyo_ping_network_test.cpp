#define main WebSocketTransportSelfTest
#include "websocket_client_test.cpp"
#undef main
#include "mihoyo/ping_probe.hpp"

int main() {
    using namespace cloudgenshin::dispatch;
    PingServer server;server.id=17;server.wss_url="wss://probe.invalid/echo";
    std::atomic_bool cancelled{false};
    fake_rtt_echo=true;
    const auto result=ProbePingServer(server,cancelled,1000);
    assert(result && *result<1000);
    const auto sent=frames(latest());unsigned probes=0;
    for(const auto& frame:sent) {
        if(frame.opcode==8) continue;
        assert(frame.opcode==1 && frame.payload=="X");++probes;
    }
    assert(probes==20);
    fake_rtt_echo=false;
    assert(!ProbePingServer(server,cancelled,100));
    cancelled=true;assert(!ProbePingServer(server,cancelled));
    cancelled=false;server.wss_url="ws://probe.invalid/echo";assert(!ProbePingServer(server,cancelled));
    server.wss_url="wss://user@probe.invalid/echo";assert(!ProbePingServer(server,cancelled));
    assert(ProbePingServers({},cancelled).node_json.empty());
    const auto invalid=ProbePingServers({server},cancelled);
    assert(invalid.tested==1 && invalid.invalid_endpoints==1 && invalid.succeeded==0);
    server.wss_url="wss://probe.invalid/echo";
    next_perform=CURLE_OPERATION_TIMEDOUT;
    next_timing={100,10000,0,0,2500000,110};
    const auto timed_out=ProbePingServers({server},cancelled);
    assert(timed_out.tested==1 && timed_out.connect_failures==1 && timed_out.connect_timeouts==1);
    assert(timed_out.first_curl_code==CURLE_OPERATION_TIMEDOUT && timed_out.first_http_status==0);
    assert(timed_out.first_transport.captured && timed_out.first_transport.total_ms==2500);
    assert(timed_out.first_transport.phase(0)==opennow::HttpFailurePhase::TlsHandshake);
    assert(timed_out.first_transport_stage==static_cast<int>(opennow::websocket::ConnectStage::TransportFailed));
    assert(timed_out.first_os_error==110 && timed_out.first_port==443);
    next_upgrade=UpgradeResponse::Forbidden;
    const auto rejected=ProbePingServers({server},cancelled);
    assert(rejected.connect_failures==1 && rejected.first_http_status==403 && rejected.connect_timeouts==0);
    assert(rejected.first_transport.phase(403)==opennow::HttpFailurePhase::ResponseStarted);
    assert(rejected.first_os_error==0);
}
