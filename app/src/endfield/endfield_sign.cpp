#include "endfield/endfield_sign.hpp"

#include "mihoyo/auth_header.hpp"

#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <utility>
#include <vector>

namespace cloudgenshin::endfield {
namespace {

constexpr std::size_t kMaximumUrl = 2048;
constexpr std::size_t kMaximumBody = 65536;
constexpr std::size_t kMaximumTimestamp = 32;
constexpr std::size_t kMaximumAuthorization = 8192;

// application/x-www-form-urlencoded "unreserved" set used by URLSearchParams:
// alphanumerics and * - . _ stay literal, space becomes '+', everything else
// is percent-encoded with uppercase hex.
bool IsFormUnreserved(unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           c == '*' || c == '-' || c == '.' || c == '_';
}

std::string PercentEncode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size());
    for (const unsigned char c : value) {
        if (IsFormUnreserved(c)) {
            out.push_back(static_cast<char>(c));
        } else if (c == ' ') {
            out.push_back('+');
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
    }
    return out;
}

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string PercentDecode(std::string_view value) {
    std::string out;
    out.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c == '+') {
            out.push_back(' ');
        } else if (c == '%' && i + 2 < value.size()) {
            const int high = HexValue(value[i + 1]);
            const int low = HexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char>((high << 4) | low));
                i += 2;
            } else {
                out.push_back(c);
            }
        } else {
            out.push_back(c);
        }
    }
    return out;
}

// `new URL(url, base).pathname` for the absolute-or-path URLs we sign.
std::string Pathname(std::string_view url) {
    std::size_t begin = 0;
    const std::size_t scheme = url.find("://");
    if (scheme != std::string_view::npos) {
        const std::size_t slash = url.find('/', scheme + 3);
        begin = slash == std::string_view::npos ? url.size() : slash;
    }
    std::size_t end = url.size();
    const std::size_t cut = url.find_first_of("?#", begin);
    if (cut != std::string_view::npos) end = cut;
    return std::string(url.substr(begin, end - begin));
}

std::string QueryString(std::string_view url) {
    const std::size_t query = url.find('?');
    if (query == std::string_view::npos) return {};
    std::size_t end = url.find('#', query);
    if (end == std::string_view::npos) end = url.size();
    return std::string(url.substr(query + 1, end - query - 1));
}

std::string UppercaseAscii(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text)
        out.push_back(c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A'))
                                           : static_cast<char>(c));
    return out;
}

}  // namespace

std::optional<std::string> Sha256Hex(std::string_view data) {
    if (data.size() > kMaximumBody) return std::nullopt;
    std::array<unsigned char, 32> digest{};
    if (mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(),
                       digest.data(), 0) != 0)
        return std::nullopt;
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(digest.size() * 2);
    for (const unsigned char byte : digest) {
        out.push_back(kHex[byte >> 4]);
        out.push_back(kHex[byte & 0x0f]);
    }
    return out;
}

std::string SortedQuery(std::string_view url) {
    struct Pair {
        std::string key;
        std::string value;
    };
    const std::string query = QueryString(url);
    if (query.empty()) return {};

    std::vector<Pair> pairs;
    std::size_t start = 0;
    while (start < query.size()) {
        const std::size_t amp = query.find('&', start);
        const std::string_view part =
            std::string_view(query).substr(start, amp == std::string::npos ? std::string::npos : amp - start);
        if (!part.empty()) {
            const std::size_t equals = part.find('=');
            Pair pair;
            if (equals == std::string_view::npos) {
                pair.key = PercentDecode(part);
            } else {
                pair.key = PercentDecode(part.substr(0, equals));
                pair.value = PercentDecode(part.substr(equals + 1));
            }
            pairs.push_back(std::move(pair));
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }

    std::stable_sort(pairs.begin(), pairs.end(), [](const Pair& a, const Pair& b) {
        if (a.key != b.key) return a.key < b.key;
        return a.value < b.value;
    });

    std::string out;
    for (const auto& pair : pairs) {
        if (!out.empty()) out.push_back('&');
        out += PercentEncode(pair.key);
        out.push_back('=');
        out += PercentEncode(pair.value);
    }
    return out;
}

std::optional<std::string> BuildSignature(std::string_view method, std::string_view url,
                                          std::string_view timestamp,
                                          std::string_view authorization_header,
                                          const std::optional<std::string>& body) {
    if (url.empty() || url.size() > kMaximumUrl) return std::nullopt;
    if (timestamp.empty() || timestamp.size() > kMaximumTimestamp) return std::nullopt;
    if (authorization_header.size() > kMaximumAuthorization) return std::nullopt;
    if (body && body->size() > kMaximumBody) return std::nullopt;

    // D(body): missing/null bodies sign as the empty string.
    const auto body_hash = Sha256Hex(body ? std::string_view(*body) : std::string_view{});
    if (!body_hash) return std::nullopt;

    std::string message = UppercaseAscii(method);
    message.push_back('\n');
    message += Pathname(url);
    message.push_back('\n');
    message += SortedQuery(url);
    message.push_back('\n');
    message += timestamp;
    message.push_back('\n');
    message += *body_hash;

    return cloudgenshin::auth::HmacSha256Hex(authorization_header, message);
}

}  // namespace cloudgenshin::endfield
