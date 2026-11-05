#include "endfield/jwe.hpp"

#include <borealis/extern/nlohmann/json.hpp>
#include <mbedtls/aes.h>
#include <mbedtls/ecp.h>
#include <mbedtls/gcm.h>
#include <mbedtls/md.h>
#include <mbedtls/platform_util.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <random>
#include <vector>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::endfield {
namespace {

constexpr std::size_t kMaximumJweBytes = 65536;
constexpr std::size_t kCoordinateBytes = 32;
constexpr std::size_t kMaximumAlgorithmName = 32;

void Random(unsigned char* bytes, std::size_t size) {
#ifdef __SWITCH__
    randomGet(bytes, size);
#else
    std::random_device random;
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<unsigned char>(random());
#endif
}

// mbedtls 3.x requires a non-NULL RNG for EC point multiplication (blinding).
int RandomForMbedtls(void* /*context*/, unsigned char* output, std::size_t size) {
    Random(output, size);
    return 0;
}

// Holds secret bytes and wipes them when the scope ends.
struct SecretBytes {
    std::vector<unsigned char> data;
    ~SecretBytes() {
        if (!data.empty()) mbedtls_platform_zeroize(data.data(), data.size());
    }
};

std::string Base64UrlEncode(const unsigned char* data, std::size_t size) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const std::uint32_t value =
            (static_cast<std::uint32_t>(data[i]) << 16) |
            (static_cast<std::uint32_t>(data[i + 1]) << 8) |
            static_cast<std::uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(value >> 18) & 63]);
        out.push_back(kAlphabet[(value >> 12) & 63]);
        out.push_back(kAlphabet[(value >> 6) & 63]);
        out.push_back(kAlphabet[value & 63]);
    }
    if (size - i == 1) {
        const std::uint32_t value = static_cast<std::uint32_t>(data[i]) << 16;
        out.push_back(kAlphabet[(value >> 18) & 63]);
        out.push_back(kAlphabet[(value >> 12) & 63]);
    } else if (size - i == 2) {
        const std::uint32_t value = (static_cast<std::uint32_t>(data[i]) << 16) |
                                    (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out.push_back(kAlphabet[(value >> 18) & 63]);
        out.push_back(kAlphabet[(value >> 12) & 63]);
        out.push_back(kAlphabet[(value >> 6) & 63]);
    }
    return out;
}

// Forgiving base64url decode, like atob() after the jose -/_ rewrite:
// padding is optional, whitespace is ignored, and a trailing partial group
// is tolerated (its spare bits must be zero).
std::optional<std::vector<unsigned char>> Base64UrlDecode(std::string_view text) {
    auto value_of = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-' || c == '+') return 62;
        if (c == '_' || c == '/') return 63;
        return -1;
    };
    std::vector<unsigned char> out;
    out.reserve(text.size() / 4 * 3 + 3);
    std::uint32_t buffer = 0;
    std::size_t bits = 0;
    std::size_t symbols = 0;
    for (const char c : text) {
        if (c == '=') break;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') continue;
        const int value = value_of(c);
        if (value < 0) return std::nullopt;
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        ++symbols;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<unsigned char>((buffer >> bits) & 0xff));
        }
    }
    if (symbols % 4 == 1) return std::nullopt;
    return out;
}

bool ConstantTimeEquals(const unsigned char* a, const unsigned char* b, std::size_t size) {
    unsigned char diff = 0;
    for (std::size_t i = 0; i < size; ++i)
        diff = static_cast<unsigned char>(diff | (a[i] ^ b[i]));
    return diff == 0;
}

void AppendBigEndian32(std::vector<unsigned char>& out, std::uint32_t value) {
    out.push_back(static_cast<unsigned char>(value >> 24));
    out.push_back(static_cast<unsigned char>(value >> 16));
    out.push_back(static_cast<unsigned char>(value >> 8));
    out.push_back(static_cast<unsigned char>(value));
}

void AppendLengthPrefixed(std::vector<unsigned char>& out, const unsigned char* data,
                          std::size_t size) {
    AppendBigEndian32(out, static_cast<std::uint32_t>(size));
    if (size != 0) out.insert(out.end(), data, data + size);
}

// SHA-256 Concat KDF, exactly the bundled jose construction (no trailing NUL
// after the algorithm id).
std::optional<std::vector<unsigned char>> ConcatKdfSha256(std::string_view algorithm_id,
                                                          const unsigned char* z,
                                                          std::size_t z_size,
                                                          const std::vector<unsigned char>& apu,
                                                          const std::vector<unsigned char>& apv,
                                                          std::size_t key_bits) {
    if (algorithm_id.empty() || algorithm_id.size() > 64) return std::nullopt;
    if (key_bits == 0 || key_bits % 8 != 0 || key_bits > 512) return std::nullopt;

    std::vector<unsigned char> other;
    other.reserve(algorithm_id.size() + apu.size() + apv.size() + 16);
    AppendLengthPrefixed(other, reinterpret_cast<const unsigned char*>(algorithm_id.data()),
                         algorithm_id.size());
    AppendLengthPrefixed(other, apu.data(), apu.size());
    AppendLengthPrefixed(other, apv.data(), apv.size());
    AppendBigEndian32(other, static_cast<std::uint32_t>(key_bits));

    const std::size_t key_bytes = key_bits / 8;
    std::vector<unsigned char> okm;
    okm.reserve(key_bytes);
    std::vector<unsigned char> input;
    input.reserve(4 + z_size + other.size());
    for (std::uint32_t counter = 1; okm.size() < key_bytes; ++counter) {
        input.clear();
        AppendBigEndian32(input, counter);
        input.insert(input.end(), z, z + z_size);
        input.insert(input.end(), other.begin(), other.end());
        std::array<unsigned char, 32> digest{};
        if (mbedtls_sha256(input.data(), input.size(), digest.data(), 0) != 0)
            return std::nullopt;
        const std::size_t take = std::min<std::size_t>(digest.size(), key_bytes - okm.size());
        okm.insert(okm.end(), digest.begin(), digest.begin() + static_cast<std::ptrdiff_t>(take));
    }
    return okm;
}

// RFC 3394 AES key unwrap.
std::optional<std::vector<unsigned char>> AesKeyUnwrap(const std::vector<unsigned char>& kek,
                                                       const std::vector<unsigned char>& wrapped) {
    if (kek.size() != 16 && kek.size() != 24 && kek.size() != 32) return std::nullopt;
    if (wrapped.size() < 24 || wrapped.size() % 8 != 0) return std::nullopt;

    const std::size_t n = wrapped.size() / 8 - 1;
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    std::optional<std::vector<unsigned char>> result;
    if (mbedtls_aes_setkey_dec(&aes, kek.data(),
                               static_cast<unsigned>(kek.size() * 8)) == 0) {
        std::array<unsigned char, 8> a{};
        std::memcpy(a.data(), wrapped.data(), a.size());
        std::vector<unsigned char> r(wrapped.begin() + 8, wrapped.end());
        bool ok = true;
        for (int round = 5; ok && round >= 0; --round) {
            for (std::size_t i = n; ok && i >= 1; --i) {
                const std::uint64_t t =
                    static_cast<std::uint64_t>(n) * static_cast<std::uint64_t>(round) + i;
                std::array<unsigned char, 16> block{};
                for (std::size_t k = 0; k < 8; ++k)
                    block[k] = static_cast<unsigned char>(
                        a[k] ^ static_cast<unsigned char>((t >> ((7 - k) * 8)) & 0xff));
                std::memcpy(block.data() + 8, r.data() + (i - 1) * 8, 8);
                std::array<unsigned char, 16> plain{};
                if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, block.data(),
                                          plain.data()) != 0) {
                    ok = false;
                    break;
                }
                std::memcpy(a.data(), plain.data(), 8);
                std::memcpy(r.data() + (i - 1) * 8, plain.data() + 8, 8);
            }
        }
        if (ok) {
            static constexpr std::array<unsigned char, 8> kInitialValue{0xa6, 0xa6, 0xa6,
                                                                        0xa6, 0xa6, 0xa6,
                                                                        0xa6, 0xa6};
            if (ConstantTimeEquals(a.data(), kInitialValue.data(), kInitialValue.size()))
                result = std::move(r);
            else
                mbedtls_platform_zeroize(r.data(), r.size());
        } else {
            mbedtls_platform_zeroize(r.data(), r.size());
        }
    }
    mbedtls_aes_free(&aes);
    return result;
}

enum class EncryptionMode { Gcm, CbcHmac };

struct EncryptionSpec {
    EncryptionMode mode = EncryptionMode::Gcm;
    std::size_t cek_bytes = 0;
    std::size_t iv_bytes = 0;
    std::size_t tag_bytes = 0;
    mbedtls_md_type_t mac_hash = MBEDTLS_MD_NONE;  // CBC-HMAC only
};

std::optional<EncryptionSpec> LookupEncryption(std::string_view enc) {
    if (enc == "A128GCM")
        return EncryptionSpec{EncryptionMode::Gcm, 16, 12, 16, MBEDTLS_MD_NONE};
    if (enc == "A192GCM")
        return EncryptionSpec{EncryptionMode::Gcm, 24, 12, 16, MBEDTLS_MD_NONE};
    if (enc == "A256GCM")
        return EncryptionSpec{EncryptionMode::Gcm, 32, 12, 16, MBEDTLS_MD_NONE};
    if (enc == "A128CBC-HS256")
        return EncryptionSpec{EncryptionMode::CbcHmac, 32, 16, 16, MBEDTLS_MD_SHA256};
    if (enc == "A192CBC-HS384")
        return EncryptionSpec{EncryptionMode::CbcHmac, 48, 16, 24, MBEDTLS_MD_SHA384};
    if (enc == "A256CBC-HS512")
        return EncryptionSpec{EncryptionMode::CbcHmac, 64, 16, 32, MBEDTLS_MD_SHA512};
    return std::nullopt;
}

struct KeyManagementSpec {
    std::size_t kek_bits = 0;       // 0: direct agreement (CEK = derived key)
    std::string algorithm_id;       // "enc" for ECDH-ES, "alg" for +AxxxKW
};

std::optional<KeyManagementSpec> LookupKeyManagement(std::string_view alg,
                                                     std::string_view enc) {
    if (alg == "ECDH-ES") return KeyManagementSpec{0, std::string(enc)};
    if (alg == "ECDH-ES+A128KW") return KeyManagementSpec{128, std::string(alg)};
    if (alg == "ECDH-ES+A192KW") return KeyManagementSpec{192, std::string(alg)};
    if (alg == "ECDH-ES+A256KW") return KeyManagementSpec{256, std::string(alg)};
    return std::nullopt;
}

}  // namespace

struct EcdhP256::Impl {
    mbedtls_ecp_group group;
    mbedtls_mpi d;
    std::string public_jwk;
    bool group_ok = false;
    bool ok = false;

    Impl() {
        mbedtls_ecp_group_init(&group);
        mbedtls_mpi_init(&d);
        group_ok = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0;
    }
    ~Impl() {
        mbedtls_mpi_free(&d);
        mbedtls_ecp_group_free(&group);
    }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    // Stores a P-256 private scalar (1 <= d < n) and derives the public key.
    bool SetScalar(const unsigned char* bytes, std::size_t size) {
        if (!group_ok || size != kCoordinateBytes) return false;
        if (mbedtls_mpi_read_binary(&d, bytes, size) != 0) return false;
        if (mbedtls_mpi_cmp_int(&d, 0) <= 0 || mbedtls_mpi_cmp_mpi(&d, &group.N) >= 0)
            return false;

        mbedtls_ecp_point public_point;
        mbedtls_ecp_point_init(&public_point);
        bool result = mbedtls_ecp_mul(&group, &public_point, &d, &group.G, RandomForMbedtls,
                                      nullptr) == 0;
        std::array<unsigned char, 65> encoded{};
        std::size_t encoded_size = 0;
        if (result)
            result = mbedtls_ecp_point_write_binary(&group, &public_point,
                                                    MBEDTLS_ECP_PF_UNCOMPRESSED, &encoded_size,
                                                    encoded.data(), encoded.size()) == 0 &&
                     encoded_size == encoded.size() && encoded[0] == 0x04;
        mbedtls_ecp_point_free(&public_point);
        if (!result) return false;

        public_jwk = "{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"" +
                     Base64UrlEncode(encoded.data() + 1, kCoordinateBytes) + "\",\"y\":\"" +
                     Base64UrlEncode(encoded.data() + 1 + kCoordinateBytes, kCoordinateBytes) +
                     "\",\"key_ops\":[\"deriveKey\",\"deriveBits\"],\"ext\":true}";
        return true;
    }

    bool GenerateRandom() {
        std::array<unsigned char, kCoordinateBytes> buffer{};
        bool result = false;
        for (int attempt = 0; attempt < 64 && !result; ++attempt) {
            Random(buffer.data(), buffer.size());
            result = SetScalar(buffer.data(), buffer.size());
        }
        mbedtls_platform_zeroize(buffer.data(), buffer.size());
        return result;
    }
};

EcdhP256::EcdhP256(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

EcdhP256::EcdhP256() : impl_(std::make_unique<Impl>()) {
    impl_->ok = impl_->GenerateRandom();
}

EcdhP256::~EcdhP256() = default;

std::optional<EcdhP256> EcdhP256::FromPrivateJwk(std::string_view jwk_json) {
    if (jwk_json.empty() || jwk_json.size() > 4096) return std::nullopt;
    const auto jwk = nlohmann::json::parse(std::string(jwk_json), nullptr, false);
    if (!jwk.is_object()) return std::nullopt;
    const auto field = [&jwk](const char* name) -> std::optional<std::string> {
        const auto iterator = jwk.find(name);
        if (iterator == jwk.end() || !iterator->is_string()) return std::nullopt;
        return iterator->get<std::string>();
    };
    const auto key_type = field("kty");
    const auto curve = field("crv");
    const auto d_text = field("d");
    if (!key_type || *key_type != "EC") return std::nullopt;
    if (!curve || *curve != "P-256") return std::nullopt;
    if (!d_text) return std::nullopt;
    const auto d = Base64UrlDecode(*d_text);
    if (!d || d->size() != kCoordinateBytes) return std::nullopt;

    auto impl = std::make_unique<Impl>();
    if (!impl->SetScalar(d->data(), d->size())) return std::nullopt;

    // Cross-check the public coordinates when the JWK carries them.
    const auto x_text = field("x");
    const auto y_text = field("y");
    if (x_text || y_text) {
        const auto computed = nlohmann::json::parse(impl->public_jwk, nullptr, false);
        if (!computed.is_object()) return std::nullopt;
        if (x_text && computed.value("x", std::string{}) != *x_text) return std::nullopt;
        if (y_text && computed.value("y", std::string{}) != *y_text) return std::nullopt;
    }
    impl->ok = true;
    return EcdhP256(std::move(impl));
}

bool EcdhP256::valid() const { return impl_ && impl_->ok; }

const std::string& EcdhP256::public_jwk() const {
    static const std::string kEmpty;
    return impl_ ? impl_->public_jwk : kEmpty;
}

std::optional<std::string> EcdhP256::DecryptTenantData(std::string_view compact_jwe,
                                                       std::string* alg_enc_metadata) const {
    if (!impl_ || !impl_->ok) return std::nullopt;
    if (compact_jwe.empty() || compact_jwe.size() > kMaximumJweBytes) return std::nullopt;

    // Compact serialization: protected.encrypted_key.iv.ciphertext.tag
    std::array<std::string_view, 5> parts{};
    {
        std::size_t start = 0;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            const std::size_t dot = compact_jwe.find('.', start);
            if (i + 1 == parts.size()) {
                if (dot != std::string_view::npos) return std::nullopt;
                parts[i] = compact_jwe.substr(start);
            } else {
                if (dot == std::string_view::npos) return std::nullopt;
                parts[i] = compact_jwe.substr(start, dot - start);
                start = dot + 1;
            }
        }
    }

    const auto protected_bytes = Base64UrlDecode(parts[0]);
    if (!protected_bytes) return std::nullopt;
    const auto header = nlohmann::json::parse(
        std::string(protected_bytes->begin(), protected_bytes->end()), nullptr, false);
    if (!header.is_object()) return std::nullopt;
    const auto alg_iterator = header.find("alg");
    const auto enc_iterator = header.find("enc");
    if (alg_iterator == header.end() || !alg_iterator->is_string()) return std::nullopt;
    if (enc_iterator == header.end() || !enc_iterator->is_string()) return std::nullopt;
    const std::string alg = alg_iterator->get<std::string>();
    const std::string enc = enc_iterator->get<std::string>();
    if (alg.empty() || alg.size() > kMaximumAlgorithmName) return std::nullopt;
    if (enc.empty() || enc.size() > kMaximumAlgorithmName) return std::nullopt;

    const auto encryption = LookupEncryption(enc);
    if (!encryption) return std::nullopt;
    const auto management = LookupKeyManagement(alg, enc);
    if (!management) return std::nullopt;

    if (alg_enc_metadata) *alg_enc_metadata = "alg=" + alg + " enc=" + enc;

    const auto iv = Base64UrlDecode(parts[2]);
    const auto ciphertext = Base64UrlDecode(parts[3]);
    const auto tag = Base64UrlDecode(parts[4]);
    if (!iv || !ciphertext || !tag) return std::nullopt;
    if (iv->size() != encryption->iv_bytes || tag->size() != encryption->tag_bytes)
        return std::nullopt;

    // Sender ephemeral public key from the (authenticated) protected header.
    const auto epk_iterator = header.find("epk");
    if (epk_iterator == header.end() || !epk_iterator->is_object()) return std::nullopt;
    const auto& epk = *epk_iterator;
    const auto epk_field = [&epk](const char* name) -> std::optional<std::string> {
        const auto iterator = epk.find(name);
        if (iterator == epk.end() || !iterator->is_string()) return std::nullopt;
        return iterator->get<std::string>();
    };
    const auto key_type = epk_field("kty");
    const auto curve = epk_field("crv");
    const auto x_text = epk_field("x");
    const auto y_text = epk_field("y");
    if (!key_type || *key_type != "EC") return std::nullopt;
    if (!curve || *curve != "P-256") return std::nullopt;
    if (!x_text || !y_text) return std::nullopt;
    const auto x = Base64UrlDecode(*x_text);
    const auto y = Base64UrlDecode(*y_text);
    if (!x || !y || x->size() != kCoordinateBytes || y->size() != kCoordinateBytes)
        return std::nullopt;

    auto decode_optional = [&header](const char* name,
                                     std::vector<unsigned char>& out) -> bool {
        const auto iterator = header.find(name);
        if (iterator == header.end() || iterator->is_null()) return true;
        if (!iterator->is_string()) return false;
        const auto decoded = Base64UrlDecode(iterator->get<std::string>());
        if (!decoded) return false;
        out = *decoded;
        return true;
    };
    std::vector<unsigned char> apu, apv;
    if (!decode_optional("apu", apu) || !decode_optional("apv", apv)) return std::nullopt;

    // ECDH: Z = x coordinate of d * epk.
    SecretBytes shared_secret;
    {
        mbedtls_ecp_point peer;
        mbedtls_ecp_point shared;
        mbedtls_ecp_point_init(&peer);
        mbedtls_ecp_point_init(&shared);
        std::array<unsigned char, 65> peer_encoded{};
        peer_encoded[0] = 0x04;
        std::memcpy(peer_encoded.data() + 1, x->data(), kCoordinateBytes);
        std::memcpy(peer_encoded.data() + 1 + kCoordinateBytes, y->data(), kCoordinateBytes);
        bool ok = mbedtls_ecp_point_read_binary(&impl_->group, &peer, peer_encoded.data(),
                                                peer_encoded.size()) == 0 &&
                  mbedtls_ecp_check_pubkey(&impl_->group, &peer) == 0 &&
                  mbedtls_ecp_mul(&impl_->group, &shared, &impl_->d, &peer, RandomForMbedtls,
                                  nullptr) == 0;
        if (ok) {
            // The shared secret is the x coordinate of d * epk; export the
            // encoded point to avoid touching private struct members.
            std::array<unsigned char, 65> shared_encoded{};
            std::size_t shared_size = 0;
            ok = mbedtls_ecp_point_write_binary(&impl_->group, &shared,
                                                MBEDTLS_ECP_PF_UNCOMPRESSED, &shared_size,
                                                shared_encoded.data(),
                                                shared_encoded.size()) == 0 &&
                 shared_size == shared_encoded.size() && shared_encoded[0] == 0x04;
            if (ok)
                shared_secret.data.assign(shared_encoded.begin() + 1,
                                          shared_encoded.begin() + 1 + kCoordinateBytes);
            mbedtls_platform_zeroize(shared_encoded.data(), shared_encoded.size());
        }
        mbedtls_ecp_point_free(&peer);
        mbedtls_ecp_point_free(&shared);
        if (!ok) return std::nullopt;
    }

    const std::size_t kek_bits =
        management->kek_bits != 0 ? management->kek_bits : encryption->cek_bytes * 8;
    SecretBytes derived;
    {
        auto kdf = ConcatKdfSha256(management->algorithm_id, shared_secret.data.data(),
                                   shared_secret.data.size(), apu, apv, kek_bits);
        if (!kdf) return std::nullopt;
        derived.data = std::move(*kdf);
    }
    mbedtls_platform_zeroize(shared_secret.data.data(), shared_secret.data.size());

    SecretBytes cek;
    if (management->kek_bits == 0) {
        // Direct key agreement: the derived key is the CEK and the
        // encrypted-key part must be absent.
        if (!parts[1].empty()) return std::nullopt;
        cek.data = std::move(derived.data);
    } else {
        const auto wrapped = Base64UrlDecode(parts[1]);
        if (!wrapped || wrapped->empty()) return std::nullopt;
        auto unwrapped = AesKeyUnwrap(derived.data, *wrapped);
        if (!unwrapped) return std::nullopt;
        cek.data = std::move(*unwrapped);
    }
    if (cek.data.size() != encryption->cek_bytes) return std::nullopt;

    // Content decryption. AAD is the raw protected header segment.
    const std::string_view aad = parts[0];
    std::string plaintext;
    if (encryption->mode == EncryptionMode::Gcm) {
        plaintext.resize(ciphertext->size());
        mbedtls_gcm_context gcm;
        mbedtls_gcm_init(&gcm);
        int code = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, cek.data.data(),
                                      static_cast<unsigned>(cek.data.size() * 8));
        if (!code)
            code = mbedtls_gcm_auth_decrypt(
                &gcm, ciphertext->size(), iv->data(), iv->size(),
                reinterpret_cast<const unsigned char*>(aad.data()), aad.size(), tag->data(),
                tag->size(), ciphertext->data(),
                reinterpret_cast<unsigned char*>(plaintext.data()));
        mbedtls_gcm_free(&gcm);
        if (code) return std::nullopt;
    } else {
        // AES-CBC + HMAC-SHA-2: mac key = first half of CEK, enc key = second
        // half; tag = first half-hash bytes of HMAC(aad || iv || ct || al).
        const std::size_t half = cek.data.size() / 2;
        const auto* mac_info = mbedtls_md_info_from_type(encryption->mac_hash);
        if (!mac_info || mbedtls_md_get_size(mac_info) < encryption->tag_bytes)
            return std::nullopt;

        bool mac_ok = false;
        {
            mbedtls_md_context_t md;
            mbedtls_md_init(&md);
            std::array<unsigned char, 64> mac{};
            std::array<unsigned char, 8> al{};
            const std::uint64_t aad_bits = static_cast<std::uint64_t>(aad.size()) * 8;
            for (std::size_t i = 0; i < al.size(); ++i)
                al[i] = static_cast<unsigned char>(aad_bits >> ((7 - i) * 8));
            if (mbedtls_md_setup(&md, mac_info, 1) == 0)
                mac_ok = mbedtls_md_hmac_starts(&md, cek.data.data(), half) == 0 &&
                         mbedtls_md_hmac_update(
                             &md, reinterpret_cast<const unsigned char*>(aad.data()),
                             aad.size()) == 0 &&
                         mbedtls_md_hmac_update(&md, iv->data(), iv->size()) == 0 &&
                         mbedtls_md_hmac_update(&md, ciphertext->data(),
                                                ciphertext->size()) == 0 &&
                         mbedtls_md_hmac_update(&md, al.data(), al.size()) == 0 &&
                         mbedtls_md_hmac_finish(&md, mac.data()) == 0;
            const bool tag_ok =
                mac_ok && ConstantTimeEquals(mac.data(), tag->data(), encryption->tag_bytes);
            mbedtls_platform_zeroize(mac.data(), mac.size());
            mbedtls_md_free(&md);
            if (!tag_ok) return std::nullopt;
        }

        if (ciphertext->empty() || ciphertext->size() % 16 != 0) return std::nullopt;
        std::array<unsigned char, 16> iv_copy{};
        std::memcpy(iv_copy.data(), iv->data(), iv_copy.size());
        plaintext.resize(ciphertext->size());
        mbedtls_aes_context aes;
        mbedtls_aes_init(&aes);
        int code = mbedtls_aes_setkey_dec(&aes, cek.data.data() + half,
                                          static_cast<unsigned>(half * 8));
        if (!code)
            code = mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, ciphertext->size(),
                                         iv_copy.data(), ciphertext->data(),
                                         reinterpret_cast<unsigned char*>(plaintext.data()));
        mbedtls_aes_free(&aes);
        if (code) return std::nullopt;

        const unsigned char pad = static_cast<unsigned char>(plaintext.back());
        if (pad == 0 || pad > 16 || pad > plaintext.size()) return std::nullopt;
        for (std::size_t i = 0; i < pad; ++i)
            if (static_cast<unsigned char>(plaintext[plaintext.size() - 1 - i]) != pad)
                return std::nullopt;
        plaintext.resize(plaintext.size() - pad);
    }
    return plaintext;
}

}  // namespace cloudgenshin::endfield
