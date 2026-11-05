#pragma once
#include "mihoyo/dispatch_response.hpp"
#include "http_client.hpp"
namespace cloudgenshin::dispatch {
inline DispatchResponse DispatchTransportFailure(const opennow::HttpRequestError& error) {
    DispatchResponse result;
    result.status=DispatchStatus::TransportError;
    result.transport_error=static_cast<int>(error.curl_code);
    result.http_status=error.http_status;
    result.transport_diagnostics=error.diagnostics;
    switch(error.curl_code) {
        case CURLE_COULDNT_RESOLVE_HOST: case CURLE_COULDNT_CONNECT:
        case CURLE_OPERATION_TIMEDOUT: case CURLE_GOT_NOTHING:
        case CURLE_SEND_ERROR: case CURLE_RECV_ERROR:
            result.retryable_transport=true;break;
        default:break; // In particular: no retry for TLS identity/certificate errors.
    }
    return result;
}
}
