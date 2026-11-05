#pragma once
#include "mihoyo/qr_login.hpp"

namespace cloudgenshin::auth {
struct ComboExchangeRequest { std::string body; std::string device; };
std::optional<ComboExchangeRequest> BuildComboExchangeRequest(
    const GameProfile& profile, const AccountLoginFields& account, const std::string& device);
QrResult ParseComboExchangeResponse(const GameProfile& profile, long http_status,
                                    const std::string& body);
QrResult ExchangeAccountForCombo(const GameProfile& profile, const ComboExchangeRequest& request,
                                const std::atomic_bool* cancelled);
} // namespace cloudgenshin::auth
