#pragma once

#include "mihoyo/auth_header.hpp"

#include <atomic>
#include <optional>
#include <string>
#include <vector>

namespace cloudgenshin {
struct GameProfile;
} // namespace cloudgenshin

namespace cloudgenshin::auth {

enum class QrStatus { Ready, Waiting, Scanned, Confirmed, Rejected, RiskRejected, VerificationRequired,
                      Expired, Cancelled, UnsupportedProtocol, HttpError,
                      TransportError, InvalidResponse };

// User-authorized Account payload is NOT already a Combo token. It must be
// verified by the official Combo login endpoint before any cloud requests.
struct AccountLoginFields { std::string uid; std::string token; };

// Secrets/challenge material stay in memory. Never print these objects or
// response bodies; render the QR only on the user's device, with no URL label.
struct QrChallenge {
    std::string url;
    std::string ticket;
};

struct QrResult {
    QrStatus status = QrStatus::InvalidResponse;
    int retcode = 0;
    long http_status = 0;
    bool verification_header_present = false;
    std::optional<QrChallenge> challenge;
    std::optional<ComboLoginFields> authorization;
    std::optional<AccountLoginFields> account_authorization;
    // Explicit passport uid/aid, or the APK's numeric open_id -> aid mapping
    // scoped to the official channel_id=1 CN Combo profile of the account's
    // own game. No other channel's opaque open_id is assumed to be a passport
    // account ID.
    std::string account_id;
    unsigned sdk_h5_additional_root_fields=0; // Count only, never names or values.
};

// The QR app_id/game_biz are the official Combo SDK values of the selected
// game profile. The same device identifier must be sent on fetch AND query:
// omitting it produces -102. A challenge is scoped to the game that issued it.
std::optional<std::vector<std::string>> BuildQrHeaders(const GameProfile& profile,
                                                       const std::string& device);
std::optional<std::string> BuildQrFetchBody(const GameProfile& profile,
                                            const std::string& device);
std::optional<std::string> BuildQrQueryBody(const GameProfile& profile,
                                            const std::string& device,
                                            const std::string& ticket);
QrResult ParseQrFetchResponse(const GameProfile& profile, long http_status,
                              const std::string& body);
QrResult ParseQrQueryResponse(const GameProfile& profile, long http_status,
                              const std::string& body);
QrResult FetchQr(const GameProfile& profile, const std::string& device,
                 const std::atomic_bool* cancelled);
QrResult QueryQr(const GameProfile& profile, const std::string& device,
                 const std::string& ticket, const std::atomic_bool* cancelled);

} // namespace cloudgenshin::auth
