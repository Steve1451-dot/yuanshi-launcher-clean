#pragma once

#include <curl/curl.h>
#include "http_transfer_diagnostics.hpp"

#include <atomic>
#include <cstddef>
#include <string>
#include <stdexcept>
#include <vector>

namespace opennow
{

bool ConfigureHttpTls(CURL* curl) noexcept;

struct HttpTransferControl
{
    const std::atomic_bool* cancelled = nullptr;
    std::size_t max_body_bytes = 0;
    bool follow_redirects = true;
    long timeout_ms = 30000;
    bool probe_verification_headers = false;
    // Optional, caller-owned, single-threaded in-memory cookie session.
    // Never supplies a COOKIEJAR path or persists cookies to disk.
    CURLSH* cookie_session = nullptr;
};

struct HttpResponse
{
    long status_code = 0;
    std::string body;
    bool aigis_header_present = false;
};

class HttpRequestError : public std::runtime_error
{
  public:
    HttpRequestError(CURLcode code,long status,const std::string& message,
                     HttpTransferDiagnostics diagnostics = {})
        : std::runtime_error(message),curl_code(code),http_status(status),diagnostics(diagnostics) {}
    CURLcode curl_code;
    long http_status;
    HttpTransferDiagnostics diagnostics;
};

class HttpClient
{
  public:
    HttpResponse Request(
        const std::string& method,
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers = {},
        const std::string& body = {},
        const std::string& proxy_url = {},
        HttpTransferControl control = {}) const;

    HttpResponse Get(
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers = {},
        const std::string& proxy_url = {},
        HttpTransferControl control = {}) const;

    HttpResponse Post(
        const std::string& url,
        const std::string& user_agent,
        const std::vector<std::string>& headers,
        const std::string& body,
        const std::string& proxy_url = {},
        HttpTransferControl control = {}) const;

    int MeasureConnectLatencyMs(
        const std::string& url,
        long timeout_ms = 3000) const noexcept;
};

} // namespace opennow
