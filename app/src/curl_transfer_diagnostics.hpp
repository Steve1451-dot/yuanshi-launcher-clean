#pragma once
#include "http_transfer_diagnostics.hpp"
#include <curl/curl.h>
#include <algorithm>

namespace opennow {
// Read before curl cleanup. Never inspect response headers, URLs or error text.
inline HttpTransferDiagnostics CaptureCurlTransferDiagnostics(CURL* curl,bool requires_tls) noexcept {
    HttpTransferDiagnostics result;
    if(!curl) return result;
    result.captured=true;result.requires_tls=requires_tls;
    const auto milliseconds=[&](CURLINFO info) {
        curl_off_t micros=0;
        if(curl_easy_getinfo(curl,info,&micros)!=CURLE_OK || micros<=0) return 0;
        // Preserve sub-millisecond completion markers and bound diagnostic ints.
        return static_cast<int>((std::min<curl_off_t>(micros,120000000)+999)/1000);
    };
    result.lookup_ms=milliseconds(CURLINFO_NAMELOOKUP_TIME_T);
    result.connect_ms=milliseconds(CURLINFO_CONNECT_TIME_T);
    result.tls_ms=milliseconds(CURLINFO_APPCONNECT_TIME_T);
    result.ready_ms=milliseconds(CURLINFO_PRETRANSFER_TIME_T);
    result.first_byte_ms=milliseconds(CURLINFO_STARTTRANSFER_TIME_T);
    result.total_ms=milliseconds(CURLINFO_TOTAL_TIME_T);
    curl_off_t uploaded=0;
    if(curl_easy_getinfo(curl,CURLINFO_SIZE_UPLOAD_T,&uploaded)==CURLE_OK)
        result.uploaded_bytes=static_cast<int>(std::clamp<curl_off_t>(uploaded,0,2147483647));
    return result;
}
}
