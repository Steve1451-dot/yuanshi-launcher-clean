#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace cloudgenshin::auth {

// Values come from an official, user-authorized combo login result and the
// current official app configuration. Never log values or persist generated
// authentication headers; authorized persistence belongs in login_store.
struct ComboLoginFields {
    std::string app_id;
    std::string channel_id;
    std::string open_id;
    std::string combo_token;
    // Bounded SDK metadata, including parsed official result.data; encrypted
    // with the rest of the login when persisted. Empty on legacy accounts.
    std::string sdk_login_data{};
};

// Shared primitive for the official sorted-field request signatures. The
// caller is responsible for the endpoint-specific field order and encoding.
std::optional<std::string> HmacSha256Hex(std::string_view key,
                                        std::string_view message);

std::optional<std::string> BuildComboTokenHeader(const ComboLoginFields& fields,
                                                  const std::string& app_key,
                                                  const std::string& game_biz);

} // namespace cloudgenshin::auth
