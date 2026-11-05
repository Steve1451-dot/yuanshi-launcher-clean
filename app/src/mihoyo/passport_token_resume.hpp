#pragma once
#include "mihoyo/launch_flow.hpp"

namespace cloudgenshin {
enum class PassportResumeSource { SToken=1, GameToken=16 };
// Official account loginBySToken -> exchange(CookieToken) -> webVerify ->
// Combo webLogin. All cookie material is transient, scoped to fixed official
// endpoints. No browser access, cookie jar, QR, queue, or vault write here.
std::optional<Authorization> ResumePassportFromAccount(const GameProfile& profile,
    const Authorization& saved, const std::atomic_bool& cancelled, const Progress& progress,
    PassportResumeSource source=PassportResumeSource::SToken);
} // namespace cloudgenshin
