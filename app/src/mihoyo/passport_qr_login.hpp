#pragma once
#include "mihoyo/qr_login.hpp"
#include <functional>
#include <memory>

namespace cloudgenshin::auth {
enum class PassportStep { Create, Query, Verify, Combo };
struct PassportIdentity {
    std::string device_id;
    std::string lifecycle_id;
    std::string device_fp{}; // omit when the official fingerprint is unavailable
};
struct PassportReply {
    QrStatus status = QrStatus::InvalidResponse;
    long http_status = 0;
    int retcode = 0;
    std::optional<QrChallenge> challenge{};
    std::string account_id{};
    std::string channel_token{}; // only the verified token_type=1 stoken
    std::optional<ComboLoginFields> combo{};
    std::string passport_mid{};
};
bool SupportsPassportLogin(const GameProfile& profile);
std::optional<std::vector<std::string>> BuildPassportHeaders(
    const GameProfile& profile, const PassportIdentity& identity, bool combo);
std::optional<std::string> BuildPassportBody(PassportStep step, const GameProfile& profile,
                                           const std::string& ticket);
PassportReply ParsePassportReply(PassportStep step, const GameProfile& profile,
                                 long http_status, const std::string& body);
// Owns only an ephemeral libcurl cookie session. Sequential requests on the
// login worker; no access to browser profiles, cookie files or saved vaults.
class PassportSession {
public:
    PassportSession(const GameProfile& profile, PassportIdentity identity,
                    const std::atomic_bool& cancelled);
    ~PassportSession();
    PassportSession(const PassportSession&) = delete;
    PassportSession& operator=(const PassportSession&) = delete;
    PassportReply request(PassportStep step, const std::string& ticket = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace cloudgenshin::auth
