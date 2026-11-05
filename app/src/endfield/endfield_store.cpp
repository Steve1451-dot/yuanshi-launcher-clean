#include "endfield/endfield_store.hpp"

#include "atomic_file_replace.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <mbedtls/gcm.h>
#include <mbedtls/platform_util.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <random>
#include <utility>

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::endfield::store {
namespace {

constexpr std::size_t kMaximum = 16384;
constexpr char kHeader[] = "CEFG0001";
constexpr char kAad[] = "CloudGenshin endfield store v1";
constexpr std::size_t kHeaderSize = sizeof(kHeader) - 1;
constexpr std::size_t kNonceSize = 12;
constexpr std::size_t kTagSize = 16;
constexpr std::size_t kOverhead = kHeaderSize + kNonceSize + kTagSize;

std::mutex store_mutex;

struct Secret {
    std::string bytes;
    ~Secret() {
        if (!bytes.empty()) mbedtls_platform_zeroize(bytes.data(), bytes.size());
    }
};

struct Key {
    std::array<unsigned char, 32> bytes{};
    ~Key() { mbedtls_platform_zeroize(bytes.data(), bytes.size()); }
};

// v1 has a passport token; v2 adds role metadata; v3 remembers the device
// proof returned by QR login. All older vaults remain usable for cloud login.
struct Decrypted {
    std::string hg_token;
    std::string uid;
    std::string device_token;
};

void Random(unsigned char* bytes, std::size_t size) {
#ifdef __SWITCH__
    randomGet(bytes, size);
#else
    std::random_device random;
    for (std::size_t i = 0; i < size; ++i) bytes[i] = static_cast<unsigned char>(random());
#endif
}

// Missing files and I/O failures stay distinguishable; reads are bounded.
enum class ReadStatus { Missing, Ok, Failed };

ReadStatus Read(const std::string& path, std::string& result, std::size_t maximum) {
    FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) return errno == ENOENT ? ReadStatus::Missing : ReadStatus::Failed;
    std::string buffer(maximum + 1, '\0');
    const auto size = std::fread(buffer.data(), 1, buffer.size(), file);
    const bool good = !std::ferror(file) && size <= maximum;
    const bool closed = std::fclose(file) == 0;
    if (!good || !closed) return ReadStatus::Failed;
    buffer.resize(size);
    result = std::move(buffer);
    return ReadStatus::Ok;
}

bool Write(const std::string& path, const std::string& bytes) {
    const auto temporary = path + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) return false;
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    file.flush();
    const bool flushed = file.good();
    file.close();
    if (!flushed || file.fail()) {
        std::remove(temporary.c_str());
        return false;
    }
    return opennow::storage::ReplaceWithTemporaryFile(temporary, path);
}

bool Remove(const std::string& path) { return std::remove(path.c_str()) == 0 || errno == ENOENT; }

bool Field(const std::string& value, std::size_t maximum) {
    return !value.empty() && value.size() <= maximum &&
           std::all_of(value.begin(), value.end(),
                       [](unsigned char c) { return c >= 33 && c <= 126; });
}

bool ReadKey(const std::string& path, Key& key) {
    Secret encoded;
    if (Read(path, encoded.bytes, 32) != ReadStatus::Ok || encoded.bytes.size() != 32) return false;
    std::copy(encoded.bytes.begin(), encoded.bytes.end(), key.bytes.begin());
    return true;
}

bool LoadKey(const std::string& path, Key& key) {
    return ReadKey(path, key) || ReadKey(path + ".bak", key);
}

std::optional<std::string> Encrypt(const std::string& plaintext, const Key& key) {
    std::array<unsigned char, kNonceSize> nonce{};
    std::array<unsigned char, kTagSize> tag{};
    Random(nonce.data(), nonce.size());
    std::string ciphertext(plaintext.size(), '\0');
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int code = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.bytes.data(), 256);
    if (!code)
        code = mbedtls_gcm_crypt_and_tag(
            &context, MBEDTLS_GCM_ENCRYPT, plaintext.size(), nonce.data(), nonce.size(),
            reinterpret_cast<const unsigned char*>(kAad), sizeof(kAad) - 1,
            reinterpret_cast<const unsigned char*>(plaintext.data()),
            reinterpret_cast<unsigned char*>(ciphertext.data()), tag.size(), tag.data());
    mbedtls_gcm_free(&context);
    if (code) return std::nullopt;
    return std::string(kHeader) +
           std::string(reinterpret_cast<char*>(nonce.data()), nonce.size()) +
           std::string(reinterpret_cast<char*>(tag.data()), tag.size()) + ciphertext;
}

std::optional<Decrypted> DecryptToken(const std::string& bytes, const Key& key) {
    if (bytes.size() <= kOverhead || !bytes.starts_with(kHeader)) return std::nullopt;
    Secret plaintext;
    plaintext.bytes.resize(bytes.size() - kOverhead);
    mbedtls_gcm_context context;
    mbedtls_gcm_init(&context);
    int code = mbedtls_gcm_setkey(&context, MBEDTLS_CIPHER_ID_AES, key.bytes.data(), 256);
    if (!code)
        code = mbedtls_gcm_auth_decrypt(
            &context, plaintext.bytes.size(),
            reinterpret_cast<const unsigned char*>(bytes.data() + kHeaderSize), kNonceSize,
            reinterpret_cast<const unsigned char*>(kAad), sizeof(kAad) - 1,
            reinterpret_cast<const unsigned char*>(bytes.data() + kHeaderSize + kNonceSize),
            kTagSize, reinterpret_cast<const unsigned char*>(bytes.data() + kOverhead),
            reinterpret_cast<unsigned char*>(plaintext.bytes.data()));
    mbedtls_gcm_free(&context);
    if (code) return std::nullopt;

    const auto value = nlohmann::json::parse(plaintext.bytes, nullptr, false);
    if (!value.is_object() || !value.contains("version") || !value["version"].is_number_integer())
        return std::nullopt;
    const bool has_uid = value["version"].get<long long>() >= 2;
    if (!value.contains("hg_token") || !value["hg_token"].is_string()) return std::nullopt;
    Decrypted token;
    token.hg_token = value["hg_token"].get<std::string>();
    if (!Field(token.hg_token, 4096)) return std::nullopt;
    // Preserve optional v2 binding metadata without requiring it in v1.
    if (has_uid) {
        if (!value.contains("uid") || !value["uid"].is_string()) return std::nullopt;
        token.uid = value["uid"].get<std::string>();
        if (!Field(token.uid, 64) && !(token.uid.empty() && value["version"].get<long long>()>=3))
            return std::nullopt;
    }
    if(value["version"].get<long long>()>=3) {
        const auto device=value.find("device_token");
        if(device==value.end() || !device->is_string()) return std::nullopt;
        token.device_token=device->get<std::string>();
        if(!Field(token.device_token,8192)) return std::nullopt;
    }
    return token;
}

std::string Serialize(const Decrypted& record, bool include_uid) {
    if(!record.device_token.empty())
        return nlohmann::json{{"version",3},{"hg_token",record.hg_token},
            {"uid",record.uid},{"device_token",record.device_token}}.dump();
    // A token-only record is vault v1: it is still valid for a later uid
    // recovery, so a failed binding lookup must not throw the token away.
    if (!include_uid)
        return nlohmann::json{{"version", 1}, {"hg_token", record.hg_token}}.dump();
    return nlohmann::json{{"version", 2}, {"hg_token", record.hg_token},
                          {"uid", record.uid}}
        .dump();
}

}  // namespace

Loaded Load(const std::string& directory) {
    std::lock_guard lock(store_mutex);
    std::string marker;
    if (Read(directory + "/endfield-login.disabled", marker, 16) != ReadStatus::Missing) return {};
    bool found = false;
    for (const auto& vault_path :
         {directory + "/endfield-login.vault", directory + "/endfield-login.vault.bak"}) {
        std::string encoded;
        const auto read = Read(vault_path, encoded, kMaximum);
        if (read == ReadStatus::Missing) continue;
        found = true;
        if (read != ReadStatus::Ok) continue;
        for (const auto& key_path :
             {directory + "/endfield-login.key", directory + "/endfield-login.key.bak"}) {
            Key key;
            if (!ReadKey(key_path, key)) continue;
            if (auto record = DecryptToken(encoded, key)) {
                return {LoadStatus::Ready, std::move(record->hg_token),
                        std::move(record->uid),std::move(record->device_token)};
            }
        }
    }
    return {found ? LoadStatus::Unreadable : LoadStatus::Missing, std::nullopt, {}, {}};
}

bool Save(const std::string& directory, const std::string& hg_token,
          const std::string& uid, int* failure_stage,const std::string& device_token) {
    std::lock_guard lock(store_mutex);
    const auto stage = [&](int value) {
        if (failure_stage) *failure_stage = value;
    };
    stage(0);
    if (!Field(hg_token, 4096)) { stage(3); return false; }
    // Device proof is independent of optional role metadata. Never discard a
    // confirmed login or require a uid to keep that proof for future launches.
    const bool include_uid = !uid.empty();
    if (include_uid && !Field(uid, 64)) { stage(3); return false; }
    if(!device_token.empty() && !Field(device_token,8192)) {stage(3);return false;}
    Key key;
    if (!LoadKey(directory + "/endfield-login.key", key)) {
        std::string previous;
        if (Read(directory + "/endfield-login.vault", previous, kMaximum) != ReadStatus::Missing ||
            Read(directory + "/endfield-login.vault.bak", previous, kMaximum) != ReadStatus::Missing)
            return false;
        Random(key.bytes.data(), key.bytes.size());
        Secret encoded{std::string(reinterpret_cast<const char*>(key.bytes.data()), key.bytes.size())};
        if (!Write(directory + "/endfield-login.key", encoded.bytes)) return false;
    }
    Secret plaintext{Serialize(Decrypted{hg_token, uid,device_token}, include_uid)};
    const auto encoded = Encrypt(plaintext.bytes, key);
    if (!encoded || encoded->size() > kMaximum || !DecryptToken(*encoded, key)) { stage(4); return false; }
    if (!Write(directory + "/endfield-login.vault", *encoded)) { stage(4); return false; }
    stage(2);  // disabled marker removal
    return Remove(directory + "/endfield-login.disabled");
}

bool Clear(const std::string& directory) {
    std::lock_guard lock(store_mutex);
    // A completed logout must not revive a backup after an interrupted delete.
    const bool disabled = Write(directory + "/endfield-login.disabled", "disabled");
    bool removed = true;
    for (const auto name : {"endfield-login.vault", "endfield-login.key"})
        for (const auto suffix : {"", ".tmp", ".bak"}) {
            const bool ok = Remove(directory + "/" + name + suffix);
            removed = ok && removed;
        }
    return disabled && removed;
}

}  // namespace cloudgenshin::endfield::store
