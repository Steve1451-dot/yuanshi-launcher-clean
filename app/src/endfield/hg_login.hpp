#pragma once

// Hypergryph passport QR login for 云·终末地, implemented from the official
// Android client (EndfieldCloud 1.1.6.0, HGSDK). Evidence and byte-level
// details: docs/cloudgenshin-endfield-findings.md §12.2.
//
// Official flow (HGSDK HGSDKQrCodeFragment, 2 s poll):
//   POST https://as.hypergryph.com/general/v1/gen_scan/login
//        {"appCode":"<cloud app>","enableRememberLogin":true}
//     -> data {scanId, scanUrl, enableScanAppList}; scanUrl is the QR payload
//   GET  https://as.hypergryph.com/general/v1/scan_status?scanId=<id>
//     -> envelope status 0 + data.scanCode = authorized
//        envelope status 100 = not scanned yet
//        envelope status 101 = scanned, waiting for phone confirmation
//        envelope status 102 = expired
//   POST https://as.hypergryph.com/user/auth/v1/token_by_scan_code
//        {"appCode":"<cloud app>","from":0,"scanCode":"<code>"}
//     -> data {token, deviceToken, rememberLogin}; token is the hg_token.
//
// The appCode is the official cloud-client value, read from the shipped
// remote config (game-config.hypergryph.com app_config_endfield →
// channel.appCode) and identical to the web client's oauth appCode.

#include <atomic>
#include <optional>
#include <string>
#include <string_view>

namespace cloudgenshin::endfield::login {

inline constexpr std::string_view kPassportHost = "https://as.hypergryph.com";
inline constexpr std::string_view kPassportAppCode = "dd7b852d5f1dd9da";
inline constexpr int kPollIntervalMs = 2000;
// The server expires the QR itself (status 102); this is only a defensive cap.
inline constexpr int kScanSessionLifetimeMs = 5 * 60 * 1000;

// HGSDK response envelope: {"status":0,"msg":"OK","data":{...},"type":"A"}.
// `data` is normally an object; the official parser also tolerates a JSON
// string, so both forms are normalized into data_json.
struct Envelope {
    bool parsed = false;
    long status = -1;
    std::string message;
    std::string data_json;
};

struct ScanSession {
    std::string scan_id;
    std::string scan_url;
};

enum class PollStatus { Waiting, Scanned, Authorized, Expired, ApiError, Invalid };

struct PollResult {
    PollStatus status = PollStatus::Invalid;
    std::string scan_code;  // Authorized only; a QR-scoped one-time code.
    long code = 0;          // envelope status for ApiError
    std::string message;    // server text, never credentials
};

struct TokenResult {
    std::string token;  // hg_token; never logged.
    // Hypergryph uid carried by the scan swap; the cloud exchange needs it to
    // obtain the u8 ticket.
    std::string uid;
    std::string device_token; // Remembered-device proof returned by the official QR exchange.
    bool remember_login = false;
};

// --- request builders (exact official key order) -------------------------
std::string BuildGenScanLoginBody();
std::string BuildTokenByScanCodeBody(std::string_view scan_code);
std::string BuildScanStatusPath(std::string_view scan_id);

// --- parsing helpers (exposed for host tests) ----------------------------
Envelope ParseEnvelope(std::string_view body);
std::optional<ScanSession> ParseGenScanLogin(const Envelope& envelope);
PollResult ParseScanStatus(const Envelope& envelope);
std::optional<TokenResult> ParseTokenByScanCode(const Envelope& envelope);

// --- HTTP (libcurl through opennow::HttpClient) --------------------------
struct HttpResult {
    long http_status = 0;
    std::string body;
};

HttpResult CreateScanSession(const std::atomic_bool* cancelled);
HttpResult PollScanStatus(std::string_view scan_id, const std::atomic_bool* cancelled);
HttpResult ExchangeScanCode(std::string_view scan_code, const std::atomic_bool* cancelled);

struct GameAuthorization {
    std::string code; // OAuth CLIENT authorization code, never a U8 token.
    long status=-1, http=0;
    bool ready() const {return status==0 && !code.empty() && code.size()<=4096;}
};
GameAuthorization AuthorizeGame(const std::string& passport_token,const std::string& device_token,
                                const std::atomic_bool* cancelled);
// The companion H5 gameManager uses checkAndUpdateToken: CLIENT grant -> U8
// tokenByOauth2Code -> cloud exchange, and only then publishes that grant's
// code. Do not substitute the APK's independent refresh for this dialect.
GameAuthorization AuthorizeWebGame(const std::string& passport_token,const std::string& device_token,
                                   const std::atomic_bool* cancelled);

}  // namespace cloudgenshin::endfield::login
