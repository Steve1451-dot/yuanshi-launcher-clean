#pragma once
#include "mihoyo/qr_login.hpp"
#include "mihoyo/gamer_login.hpp"
#include "mihoyo/dispatch_response.hpp"
#include <functional>
#include <chrono>

namespace cloudgenshin {
// Account data can be saved by the user-authorized login store. Game startup
// values remain ephemeral; callbacks carry no HTTP body or credentials.
struct Authorization {
    auth::ComboLoginFields combo;
    auth::WebDeviceIdentity device;
    std::string account_id;
    // Account token from the user-authorized account login. The old Combo-QR
    // Account payload did not prove its token type; Passport Verify explicitly
    // verifies token_type=1 (SToken). Never infer type just from this field name.
    // The in-game SDK login expects it back as channel_token; empty when the
    // login mode did not expose it. Persisted only inside the encrypted vault.
    std::string channel_token{};
    // Optional service-issued Passport session context, encrypted with the
    // account token. Older QR/vaults omit it; never fabricate or log its value.
    std::string passport_mid{};
};
using Progress = std::function<void(const std::string& event, int code, long http)>;
enum class ResumeStatus { Valid, Rejected, RetryLater, Cancelled };
struct LaunchOptions {bool wait_in_long_normal_queue=false;};
inline constexpr std::chrono::minutes QueueWaitLimit(LaunchOptions options) {
    return std::chrono::minutes(options.wait_in_long_normal_queue?30:120);
}
ResumeStatus ResumeLogin(const GameProfile& profile, const Authorization& authorization,
                         const std::atomic_bool& cancelled, const Progress& progress,
                         std::optional<Authorization>* refreshed=nullptr);
// Star Rail's in-game SDK login needs a separate fresh grant after dispatch.
// One official native Combo exchange, no cloud-login/queue replay or vault write.
// Null on unsupported profile, cancellation or failure; the input stays intact.
std::optional<auth::ComboLoginFields> RefreshGameSdkLogin(const GameProfile& profile,
    const Authorization& authorization, const std::atomic_bool& cancelled, const Progress& progress);
// With a refresh output, TOKEN_INVALID may perform one official account ->
// Combo exchange and one validation. The old authorization/output are never
// changed on failure; callers persist a successful replacement atomically.
std::optional<Authorization> Login(const GameProfile& profile, const std::atomic_bool& cancelled,
    const std::function<void(const std::string&)>& render_qr, const Progress& progress);
std::optional<dispatch::SdkStartGameParams> Launch(const GameProfile& profile,
    const Authorization& authorization, const std::atomic_bool& cancelled,
    const Progress& progress,LaunchOptions options={});
} // namespace cloudgenshin
