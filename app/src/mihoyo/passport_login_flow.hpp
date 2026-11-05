#pragma once
#include "mihoyo/passport_qr_login.hpp"
#include "mihoyo/launch_flow.hpp"
#include <chrono>

namespace cloudgenshin {
struct PassportLoginTransport {
    std::function<auth::PassportReply(auth::PassportStep,const std::string&)> request;
    std::function<bool(std::chrono::milliseconds)> wait;
};
// A deterministic orchestration seam for host tests. Transport callbacks must
// keep their cookie session private and never return/print HTTP response text.
std::optional<Authorization> RunPassportLogin(const GameProfile& profile,
    const auth::PassportIdentity& identity,const std::atomic_bool& cancelled,
    const std::function<void(const std::string&)>& render_qr,const Progress& progress,
    const PassportLoginTransport& transport);
std::optional<Authorization> LoginPassport(const GameProfile& profile,
    const std::atomic_bool& cancelled,const std::function<void(const std::string&)>& render_qr,
    const Progress& progress);
} // namespace cloudgenshin
