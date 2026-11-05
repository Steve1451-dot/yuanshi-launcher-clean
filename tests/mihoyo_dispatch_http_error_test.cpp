#include "mihoyo/dispatch_http_error.hpp"
#include <cassert>
int main() {
    using namespace cloudgenshin::dispatch;
    for(const auto code:{CURLE_COULDNT_RESOLVE_HOST,CURLE_COULDNT_CONNECT,
        CURLE_OPERATION_TIMEDOUT,CURLE_GOT_NOTHING,CURLE_SEND_ERROR,CURLE_RECV_ERROR}) {
        const auto r=DispatchTransportFailure({code,0,"synthetic private exception text"});
        assert(r.status==DispatchStatus::TransportError && r.transport_error==code && r.retryable_transport);
        assert(r.ticket.empty() && r.sid.empty() && !r.sdk_start_game_params && r.retcode==0);
    }
    for(const auto code:{CURLE_PEER_FAILED_VERIFICATION,CURLE_SSL_CACERT_BADFILE,
        CURLE_ABORTED_BY_CALLBACK,CURLE_WRITE_ERROR,CURLE_URL_MALFORMAT}) {
        const auto r=DispatchTransportFailure({code,503,"synthetic private exception text"});
        assert(!r.retryable_transport && r.transport_error==code && r.http_status==503);
    }
    const auto r=DispatchTransportFailure({CURLE_OPERATION_TIMEDOUT,0,"synthetic secret",
        {.captured=true,.requires_tls=true,.lookup_ms=1,.connect_ms=5,.total_ms=10000}});
    assert(r.transport_diagnostics.phase(0)==opennow::HttpFailurePhase::TlsHandshake);
}
