#pragma once

// Request signing for the cloud-game API (official web bundle: X-Signature).
//
// message = METHOD.toUpperCase() \n pathname \n sortedQuery \n timestamp \n sha256hex(body)
// key     = "" (unauthenticated) or "Bearer <login_session_id>"
//
// sortedQuery mirrors URLSearchParams: query parameters sorted by key then
// value, serialized as application/x-www-form-urlencoded.
// sha256hex and the HMAC use lowercase hex, like the reference client.

#include <optional>
#include <string>
#include <string_view>

namespace cloudgenshin::endfield {

std::optional<std::string> Sha256Hex(std::string_view data);

// URLSearchParams-equivalent sorted+encoded query of a URL/path ("" if none).
std::string SortedQuery(std::string_view url);

// body: std::nullopt for requests without a body (signed as empty string).
std::optional<std::string> BuildSignature(std::string_view method,
                                          std::string_view url,
                                          std::string_view timestamp,
                                          std::string_view authorization_header,
                                          const std::optional<std::string>& body);

}  // namespace cloudgenshin::endfield
