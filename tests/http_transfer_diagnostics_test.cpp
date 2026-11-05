#include "http_transfer_diagnostics.hpp"
#include <cassert>
int main() {
    using namespace opennow;
    HttpTransferDiagnostics d;
    assert(d.phase(0)==HttpFailurePhase::Unknown);
    d.captured=true;d.requires_tls=true;d.total_ms=10000;
    assert(d.phase(0)==HttpFailurePhase::Resolving);
    d.lookup_ms=1;
    assert(d.phase(0)==HttpFailurePhase::Connecting);
    d.connect_ms=100;
    assert(d.phase(0)==HttpFailurePhase::TlsHandshake);
    d.tls_ms=180;d.ready_ms=181;
    assert(d.phase(0)==HttpFailurePhase::AwaitingResponse);
    d.ready_ms=0;
    assert(d.phase(0)==HttpFailurePhase::AwaitingResponse);
    d.first_byte_ms=200;
    assert(d.phase(0)==HttpFailurePhase::ResponseStarted);
    d.first_byte_ms=0;
    assert(d.phase(503)==HttpFailurePhase::ResponseStarted);
    d.requires_tls=false;d.tls_ms=0;
    assert(d.phase(0)==HttpFailurePhase::AwaitingResponse);
}
