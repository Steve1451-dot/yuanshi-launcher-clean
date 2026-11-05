#include "mihoyo/auth_header.hpp"

#include <mbedtls/sha256.h>

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace cloudgenshin::auth {
namespace {

constexpr std::size_t kSha256Bytes = 32;
constexpr std::size_t kSha256BlockBytes = 64;

bool HeaderValueSafe(std::string_view value) {
    return value.size() <= 4096 && value.find_first_of("\r\n;") == std::string_view::npos;
}

} // namespace

std::optional<std::string> HmacSha256Hex(std::string_view key, std::string_view message) {
    std::array<unsigned char, kSha256BlockBytes> key_block{};
    if (key.size() > key_block.size()) {
        if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(key.data()), key.size(),
                           key_block.data(), 0) != 0)
            return std::nullopt;
    } else {
        for (std::size_t i = 0; i < key.size(); ++i)
            key_block[i] = static_cast<unsigned char>(key[i]);
    }

    std::vector<unsigned char> inner(kSha256BlockBytes + message.size());
    std::vector<unsigned char> outer(kSha256BlockBytes + kSha256Bytes);
    for (std::size_t i = 0; i < kSha256BlockBytes; ++i) {
        inner[i] = key_block[i] ^ 0x36;
        outer[i] = key_block[i] ^ 0x5c;
    }
    for (std::size_t i = 0; i < message.size(); ++i)
        inner[kSha256BlockBytes + i] = static_cast<unsigned char>(message[i]);
    if (mbedtls_sha256(inner.data(), inner.size(), outer.data() + kSha256BlockBytes, 0) != 0)
        return std::nullopt;
    std::array<unsigned char, kSha256Bytes> digest{};
    if (mbedtls_sha256(outer.data(), outer.size(), digest.data(), 0) != 0)
        return std::nullopt;
    constexpr char kHex[] = "0123456789abcdef";
    std::string result;
    result.reserve(kSha256Bytes * 2);
    for (unsigned char byte : digest) {
        result.push_back(kHex[byte >> 4]);
        result.push_back(kHex[byte & 0x0f]);
    }
    return result;
}

std::optional<std::string> BuildComboTokenHeader(const ComboLoginFields& fields,
                                                  const std::string& app_key,
                                                  const std::string& game_biz) {
    if (app_key.empty() || game_biz.empty() || !HeaderValueSafe(fields.app_id) ||
        !HeaderValueSafe(fields.channel_id) || !HeaderValueSafe(fields.open_id) ||
        !HeaderValueSafe(fields.combo_token) || !HeaderValueSafe(app_key) ||
        !HeaderValueSafe(game_biz))
        return std::nullopt;
    // Exact key order and field names used by the official web client.
    const std::string signed_fields = "app_id=" + fields.app_id +
        "&channel_id=" + fields.channel_id + "&combo_token=" + fields.combo_token +
        "&open_id=" + fields.open_id;
    const auto signature = HmacSha256Hex(app_key, signed_fields);
    if (!signature) return std::nullopt;
    return "ai=" + fields.app_id + ";ci=" + fields.channel_id + ";oi=" +
        fields.open_id + ";ct=" + fields.combo_token + ";si=" + *signature +
        ";bi=" + game_biz;
}

} // namespace cloudgenshin::auth
