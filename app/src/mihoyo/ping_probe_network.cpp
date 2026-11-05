#include "mihoyo/ping_probe.hpp"
#include "WebSocketClient.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <thread>

namespace cloudgenshin::dispatch {
namespace {
using Clock=std::chrono::steady_clock;
struct ProbeFailure {
    enum class Stage { None, Endpoint, Connect, Echo } stage=Stage::None;
    int curl_code=0;long http_status=0;bool timeout=false;
    opennow::HttpTransferDiagnostics transfer{};
    int transport_stage=0,elapsed_ms=0,port=0,os_error=0;
};
bool Endpoint(const std::string& url) {
    if(!url.starts_with("wss://") || url.size()>2048 || url.find('#')!=url.npos) return false;
    const auto end=url.find_first_of("/?",6);
    const auto host=url.substr(6,end==url.npos ? url.npos : end-6);
    if(host.empty() || host.find('@')!=host.npos) return false;
    for(const unsigned char c:url) if(c<0x21 || c==0x7f) return false;
    return true;
}
std::optional<unsigned> Probe(const PingServer& server,const std::atomic_bool& cancelled,
                              Clock::time_point overall_deadline,ProbeFailure* failure=nullptr) {
    if(cancelled) return std::nullopt;
    if(!Endpoint(server.wss_url)) {
        if(failure) failure->stage=ProbeFailure::Stage::Endpoint;
        return std::nullopt;
    }
    const auto deadline=std::min(overall_deadline,Clock::now()+std::chrono::milliseconds(2500));
    WebSocketClient socket(server.wss_url);
    std::deque<Clock::time_point> sent;
    unsigned replies=0;double total_ms=0;
    const auto pong=[&] {
        if(sent.empty() || replies==20) return;
        total_ms+=std::chrono::duration<double,std::milli>(Clock::now()-sent.front()).count();
        sent.pop_front();++replies;
    };
    socket.set_on_message([&](const std::string&){pong();});
    socket.set_on_binary_message([&](const std::vector<std::uint8_t>&){pong();});
    const auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
    if(remaining<100 || !socket.connect(&cancelled,static_cast<long>(remaining))) {
        if(failure) {
            const auto& diagnostic=socket.connect_diagnostics();
            failure->stage=ProbeFailure::Stage::Connect;
            failure->curl_code=diagnostic.curl_code;failure->http_status=diagnostic.http_status;
            failure->transfer=diagnostic.transfer;
            failure->transport_stage=static_cast<int>(diagnostic.stage);
            failure->elapsed_ms=diagnostic.elapsed_ms;failure->port=diagnostic.endpoint_port;
            failure->os_error=diagnostic.os_error;
            failure->timeout=remaining<100 || diagnostic.curl_code==CURLE_OPERATION_TIMEDOUT ||
                diagnostic.stage==opennow::websocket::ConnectStage::HandshakeTimeout;
        }
        return std::nullopt;
    }
    // Official WebSocketRttTester: twenty TEXT "X" sends and FIFO echoes.
    // No game authorization/hello or guessed ping JSON is sent to this socket.
    for(unsigned i=0;i<20 && !cancelled && Clock::now()<deadline;++i) {
        sent.push_back(Clock::now());socket.send_message("X");
    }
    while(!cancelled && replies<20 && socket.is_connected() && Clock::now()<deadline) {
        socket.poll();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    socket.disconnect();
    if(cancelled) return std::nullopt;
    if(replies!=20) {if(failure) failure->stage=ProbeFailure::Stage::Echo;return std::nullopt;}
    return static_cast<unsigned>(total_ms/20+0.5);
}
}
std::optional<unsigned> ProbePingServer(const PingServer& server,const std::atomic_bool& cancelled,long budget_ms) {
    if(budget_ms<100) return std::nullopt;
    return Probe(server,cancelled,Clock::now()+std::chrono::milliseconds(std::min(budget_ms,2500L)));
}
PingProbeResult ProbePingServers(const std::vector<PingServer>& servers,const std::atomic_bool& cancelled) {
    PingProbeResult result;
    if(cancelled || servers.empty()) return result;
    // Probe only service-returned endpoints. Bounded to 32 targets / 4 workers,
    // 12 seconds overall, 2.5 seconds per target; cancellation joins workers.
    const auto count=std::min<std::size_t>(servers.size(),32);
    const auto deadline=Clock::now()+std::chrono::seconds(12);
    std::vector<std::optional<unsigned>> samples(count);
    std::vector<ProbeFailure> failures(count);
    std::atomic_size_t next{0};std::atomic_uint tested{0};
    std::vector<std::jthread> workers;
    for(std::size_t worker=0;worker<std::min<std::size_t>(count,4);++worker) {
        workers.emplace_back([&] {
            while(!cancelled && Clock::now()<deadline) {
                const auto index=next.fetch_add(1);if(index>=count) break;
                ++tested;
                try {samples[index]=Probe(servers[index],cancelled,deadline,&failures[index]);}
                catch (...) {failures[index].stage=ProbeFailure::Stage::Connect;}
            }
        });
    }
    for(auto& worker:workers) worker.join();
    result.tested=tested;
    if(cancelled) return result;
    std::vector<PingSample> succeeded;
    bool first_failure=true;
    for(std::size_t i=0;i<count;++i) {
        if(samples[i]) {succeeded.push_back({servers[i],*samples[i]});continue;}
        const auto& failure=failures[i];
        if(failure.stage==ProbeFailure::Stage::Endpoint) ++result.invalid_endpoints;
        if(failure.stage==ProbeFailure::Stage::Connect) ++result.connect_failures;
        if(failure.stage==ProbeFailure::Stage::Echo) ++result.echo_failures;
        if(failure.timeout) ++result.connect_timeouts;
        if(first_failure && failure.stage!=ProbeFailure::Stage::None) {
            result.first_curl_code=failure.curl_code;result.first_http_status=failure.http_status;
            result.first_transport=failure.transfer;result.first_transport_stage=failure.transport_stage;
            result.first_elapsed_ms=failure.elapsed_ms;result.first_port=failure.port;
            result.first_os_error=failure.os_error;
            first_failure=false;
        }
    }
    result.succeeded=static_cast<unsigned>(succeeded.size());
    if(const auto json=BuildMeasuredPingNode(succeeded)) result.node_json=*json;
    return result;
}
} // namespace cloudgenshin::dispatch
