#pragma once

// Compact JWE support for the official login exchange.
//
// The reference web client generates an ephemeral ECDH P-256 key with
// crypto.subtle, sends its public JWK as "epk_jwk", and decrypts the
// vlink_tenant_data answer with a bundled jose implementation. This module
// re-implements exactly that decryption path:
//
//  - alg: ECDH-ES (direct key agreement) or ECDH-ES+A128KW/+A192KW/+A256KW
//    (key agreement with AES key wrap);
//  - enc: A128/192/256GCM or A128CBC-HS256 / A192CBC-HS384 / A256CBC-HS512;
//  - SHA-256 Concat KDF with the bundled jose OtherInfo construction:
//    length-prefixed algorithm id (the "enc" value for ECDH-ES, the "alg"
//    value for +AxxxKW), length-prefixed apu/apv, then the key length in
//    bits, and no trailing NUL;
//  - AAD = the raw base64url protected header segment.

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace cloudgenshin::endfield {

class EcdhP256 {
public:
    EcdhP256();
    EcdhP256(EcdhP256&&) = default;
    EcdhP256& operator=(EcdhP256&&) = default;
    ~EcdhP256();
    EcdhP256(const EcdhP256&) = delete;
    EcdhP256& operator=(const EcdhP256&) = delete;

    // The reference client can decrypt with any imported private JWK; this
    // factory mirrors that and lets published known-answer vectors drive the
    // implementation. Expects the crypto.subtle exportKey("jwk") shape
    // (kty EC, crv P-256, d; x/y cross-checked when present).
    static std::optional<EcdhP256> FromPrivateJwk(std::string_view jwk_json);

    bool valid() const;

    // JSON string of the public JWK in the same shape crypto.subtle
    // exportKey("jwk") produces: kty, crv, x, y, key_ops, ext.
    const std::string& public_jwk() const;

    // Decrypts one compact JWE addressed to this key. On success, when
    // alg_enc_metadata is non-null it receives "alg=<alg> enc=<enc>"
    // (algorithm names only, safe for diagnostics).
    std::optional<std::string> DecryptTenantData(std::string_view compact_jwe,
                                                 std::string* alg_enc_metadata = nullptr) const;

private:
    struct Impl;
    explicit EcdhP256(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace cloudgenshin::endfield
