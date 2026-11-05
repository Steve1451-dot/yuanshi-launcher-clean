#pragma once

namespace opennow {

// Only fixed numeric transport metadata. No URLs, headers, bodies or addresses.
enum class HttpFailurePhase { Unknown, Resolving, Connecting, TlsHandshake, AwaitingResponse, ResponseStarted };
struct HttpTransferDiagnostics {
    bool captured = false;
    bool requires_tls = false;
    int lookup_ms = 0;
    int connect_ms = 0;
    int tls_ms = 0;
    int ready_ms = 0;
    int first_byte_ms = 0;
    int total_ms = 0;
    int uploaded_bytes = 0;

    HttpFailurePhase phase(long http_status) const noexcept {
        if (!captured) return HttpFailurePhase::Unknown;
        if (first_byte_ms > 0 || http_status > 0) return HttpFailurePhase::ResponseStarted;
        // CONNECT_ONLY transports can finish TLS without PRETRANSFER being set.
        if (ready_ms > 0 || tls_ms > 0 || (!requires_tls && connect_ms > 0))
            return HttpFailurePhase::AwaitingResponse;
        if (requires_tls && connect_ms > 0 && tls_ms == 0) return HttpFailurePhase::TlsHandshake;
        if (lookup_ms > 0) return HttpFailurePhase::Connecting;
        return HttpFailurePhase::Resolving;
    }
};

} // namespace opennow
