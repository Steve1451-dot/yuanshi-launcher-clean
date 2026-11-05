#include "mihoyo/login_store.hpp"
#include "mihoyo/production_profile.hpp"
#include "mihoyo/combo_login_data.hpp"
#include "mihoyo/passport_signin_policy.hpp"
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
#ifdef __SWITCH__
#include <switch.h>
#endif

namespace cloudgenshin::login_store {
namespace {
constexpr std::size_t kMaximum=16384;
constexpr char kHeader[]="CGLGN001";
constexpr char kAad[]="CloudGenshin login store v1";
std::mutex store_mutex;
struct Secret {
    std::string bytes;
    ~Secret(){if(!bytes.empty()) mbedtls_platform_zeroize(bytes.data(),bytes.size());}
};
struct Key {
    std::array<unsigned char,32> bytes{};
    ~Key(){mbedtls_platform_zeroize(bytes.data(),bytes.size());}
};
bool Random(unsigned char* bytes,std::size_t size) {
#ifdef __SWITCH__
    randomGet(bytes,size);
#else
    std::random_device random;for(std::size_t i=0;i<size;++i) bytes[i]=static_cast<unsigned char>(random());
#endif
    return true;
}
// Missing files and I/O failures stay distinguishable; reads are bounded.
enum class ReadStatus { Missing, Ok, Failed };
ReadStatus Read(const std::string& path,std::string& result,std::size_t maximum) {
    FILE* file=std::fopen(path.c_str(),"rb");
    if(!file) return errno==ENOENT ? ReadStatus::Missing : ReadStatus::Failed;
    std::string buffer(maximum+1,'\0');
    const auto size=std::fread(buffer.data(),1,buffer.size(),file);
    const bool good=!std::ferror(file) && size<=maximum;
    const bool closed=std::fclose(file)==0;
    if(!good || !closed) return ReadStatus::Failed;
    buffer.resize(size);result=std::move(buffer);return ReadStatus::Ok;
}
bool Write(const std::string& path,const std::string& bytes) {
    const auto temporary=path+".tmp";
    std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
    if(!file) return false;
    file.write(bytes.data(),static_cast<std::streamsize>(bytes.size()));file.flush();
    const bool flushed=file.good();file.close();
    if(!flushed || file.fail()) {std::remove(temporary.c_str());return false;}
    return opennow::storage::ReplaceWithTemporaryFile(temporary,path);
}
bool Remove(const std::string& path) {return std::remove(path.c_str())==0 || errno==ENOENT;}
bool Digits(const std::string& value,std::size_t maximum) {
    return !value.empty() && value.size()<=maximum && std::all_of(value.begin(),value.end(),[](char c){return c>='0' && c<='9';});
}
bool Field(const std::string& value,std::size_t maximum) {
    return !value.empty() && value.size()<=maximum && std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=33 && c<=126;});
}
bool Valid(const Authorization& authorization) {
    return Digits(authorization.combo.app_id,10) && Digits(authorization.combo.channel_id,10) &&
        Field(authorization.combo.open_id,256) && Field(authorization.combo.combo_token,4096) &&
        Digits(authorization.account_id,32) && Field(authorization.device.device_id,128) &&
        auth::DecodeComboSdkData(authorization.combo.sdk_login_data).has_value() &&
        (authorization.channel_token.empty() || Field(authorization.channel_token,4096)) &&
        (authorization.passport_mid.empty() ||
            (!authorization.channel_token.empty() && auth::ValidPassportMid(authorization.passport_mid)));
}
nlohmann::json SerializeEntry(const GameProfile& profile,const Authorization& authorization) {
    nlohmann::json entry{{"game",profile.game_biz},{"app_id",authorization.combo.app_id},
        {"channel_id",authorization.combo.channel_id},{"open_id",authorization.combo.open_id},
        {"combo_token",authorization.combo.combo_token},{"account_id",authorization.account_id},
        {"device_id",authorization.device.device_id}};
    // Optional vault field: older readers ignore it, older vaults omit it.
    if(!authorization.channel_token.empty()) entry["channel_token"]=authorization.channel_token;
    if(!authorization.passport_mid.empty()) entry["passport_mid"]=authorization.passport_mid;
    if(!authorization.combo.sdk_login_data.empty())
        entry["sdk_login_data"]=*auth::DecodeComboSdkData(authorization.combo.sdk_login_data);
    return entry;
}
struct Entry {
    const GameProfile* profile = nullptr;
    Authorization authorization;
    // Data-only compatibility for a removed game. Never a selectable profile
    // or authorization; preserve it encrypted when saving a supported game.
    nlohmann::json retired = nullptr;
    std::string_view game_biz() const {
        return profile ? profile->game_biz : std::string_view(retired["game"].get_ref<const std::string&>());
    }
};
std::string Serialize(const std::vector<Entry>& entries) {
    nlohmann::json list=nlohmann::json::array();
    for(const auto& entry:entries)
        list.push_back(entry.profile ? SerializeEntry(*entry.profile,entry.authorization) : entry.retired);
    return nlohmann::json{{"version",2},{"entries",std::move(list)}}.dump();
}
std::optional<Entry> ParseEntry(const nlohmann::json& value) {
    for(const auto field:{"game","app_id","channel_id","open_id","combo_token","account_id","device_id"})
        if(!value.contains(field) || !value[field].is_string()) return std::nullopt;
    const auto* profile=FindGameProfileByBiz(value["game"].get<std::string>());
    if(!profile && value["game"]=="nap_cn" && value["app_id"]=="12") {
        if(!Digits(value["channel_id"].get<std::string>(),10) ||
           !Field(value["open_id"].get<std::string>(),256) || !Field(value["combo_token"].get<std::string>(),4096) ||
           !Digits(value["account_id"].get<std::string>(),32) || !Field(value["device_id"].get<std::string>(),128)) return {};
        if(value.contains("sdk_login_data") &&
           (!value["sdk_login_data"].is_object() || !auth::DecodeComboSdkData(value["sdk_login_data"].dump()))) return {};
        return Entry{nullptr,{},value};
    }
    if(!profile) return std::nullopt;
    Authorization result{{value["app_id"].get<std::string>(),value["channel_id"].get<std::string>(),
        value["open_id"].get<std::string>(),value["combo_token"].get<std::string>()},
        NativeWebIdentity(*profile,value["device_id"].get<std::string>()),
        value["account_id"].get<std::string>(),{}};
    // Optional v2 extension. Existing v1/v2 vaults remain readable, and an
    // invalid extension is never silently converted into a successful login.
    if(value.contains("sdk_login_data")) {
        if(!value["sdk_login_data"].is_object()) return std::nullopt;
        result.combo.sdk_login_data=value["sdk_login_data"].dump();
    }
    // channel_token is optional: vaults written before it existed stay valid,
    // and a malformed value is dropped instead of rejecting the whole entry.
    if(value.contains("channel_token") && value["channel_token"].is_string()) {
        const auto token=value["channel_token"].get<std::string>();
        if(Field(token,4096)) result.channel_token=token;
    }
    // Invalid optional web context must not invalidate an unrelated game's
    // old login. Discard it and require explicit web sign-in instead.
    if(!result.channel_token.empty() && value.contains("passport_mid") && value["passport_mid"].is_string()) {
        const auto mid=value["passport_mid"].get<std::string>();
        if(auth::ValidPassportMid(mid)) result.passport_mid=mid;
    }
    if(!Valid(result) || result.combo.app_id!=AppIdText(*profile)) return std::nullopt;
    return Entry{profile,std::move(result)};
}
std::optional<std::vector<Entry>> Parse(const std::string& bytes) {
    const auto value=nlohmann::json::parse(bytes,nullptr,false);
    if(!value.is_object() || !value.contains("version") ||
       !value["version"].is_number_integer()) return std::nullopt;
    std::vector<Entry> entries;
    if(value["version"]==1) {
        // Legacy single-entry vault: always the default (hk4e) game.
        auto entry=value;
        entry["game"]=std::string(DefaultGameProfile().game_biz);
        const auto parsed=ParseEntry(entry);
        if(!parsed) return std::nullopt;
        entries.push_back(*parsed);
        return entries;
    }
    if(value["version"]!=2 || !value.contains("entries") || !value["entries"].is_array() ||
       value["entries"].empty() || value["entries"].size()>8) return std::nullopt;
    for(const auto& item:value["entries"]) {
        if(!item.is_object()) return std::nullopt;
        const auto parsed=ParseEntry(item);
        if(!parsed) return std::nullopt;
        for(const auto& existing:entries)
            if(existing.game_biz()==parsed->game_biz()) return std::nullopt;
        entries.push_back(*parsed);
    }
    return entries;
}
bool ReadKey(const std::string& path,Key& key) {
    Secret encoded;
    if(Read(path,encoded.bytes,32)!=ReadStatus::Ok || encoded.bytes.size()!=32) return false;
    std::copy(encoded.bytes.begin(),encoded.bytes.end(),key.bytes.begin());return true;
}
bool LoadKey(const std::string& path,Key& key) {
    return ReadKey(path,key) || ReadKey(path+".bak",key);
}
std::optional<std::string> Encrypt(const std::string& plaintext,const Key& key) {
    std::array<unsigned char,12> nonce{};std::array<unsigned char,16> tag{};Random(nonce.data(),nonce.size());
    std::string ciphertext(plaintext.size(),'\0');mbedtls_gcm_context context;mbedtls_gcm_init(&context);
    int code=mbedtls_gcm_setkey(&context,MBEDTLS_CIPHER_ID_AES,key.bytes.data(),256);
    if(!code) code=mbedtls_gcm_crypt_and_tag(&context,MBEDTLS_GCM_ENCRYPT,plaintext.size(),nonce.data(),nonce.size(),
        reinterpret_cast<const unsigned char*>(kAad),sizeof(kAad)-1,
        reinterpret_cast<const unsigned char*>(plaintext.data()),reinterpret_cast<unsigned char*>(ciphertext.data()),tag.size(),tag.data());
    mbedtls_gcm_free(&context);if(code) return std::nullopt;
    return std::string(kHeader,sizeof(kHeader)-1)+std::string(reinterpret_cast<char*>(nonce.data()),nonce.size())+
        std::string(reinterpret_cast<char*>(tag.data()),tag.size())+ciphertext;
}
std::optional<std::vector<Entry>> DecryptEntries(const std::string& bytes,const Key& key) {
    if(bytes.size()<=36 || !bytes.starts_with(kHeader)) return std::nullopt;
    Secret plaintext;plaintext.bytes.resize(bytes.size()-36);
    mbedtls_gcm_context context;mbedtls_gcm_init(&context);
    int code=mbedtls_gcm_setkey(&context,MBEDTLS_CIPHER_ID_AES,key.bytes.data(),256);
    if(!code) code=mbedtls_gcm_auth_decrypt(&context,plaintext.bytes.size(),
        reinterpret_cast<const unsigned char*>(bytes.data()+8),12,
        reinterpret_cast<const unsigned char*>(kAad),sizeof(kAad)-1,
        reinterpret_cast<const unsigned char*>(bytes.data()+20),16,
        reinterpret_cast<const unsigned char*>(bytes.data()+36),reinterpret_cast<unsigned char*>(plaintext.bytes.data()));
    mbedtls_gcm_free(&context);return code ? std::nullopt : Parse(plaintext.bytes);
}
}
Loaded Load(const std::string& directory,const GameProfile& profile) {
    std::lock_guard lock(store_mutex);
    std::string marker;
    if(Read(directory+"/cloudgenshin-login.disabled",marker,16)!=ReadStatus::Missing) return {};
    Key key;bool found=false,readable=false;
    for(const auto& path:{directory+"/cloudgenshin-login.vault",directory+"/cloudgenshin-login.vault.bak"}) {
        std::string encoded;const auto read=Read(path,encoded,kMaximum);
        if(read==ReadStatus::Missing) continue;
        found=true;
        if(read!=ReadStatus::Ok) continue;
        for(const auto& key_path:{directory+"/cloudgenshin-login.key",directory+"/cloudgenshin-login.key.bak"}) {
            if(!ReadKey(key_path,key)) continue;
            if(auto entries=DecryptEntries(encoded,key)) {
                readable=true;
                for(auto& entry:*entries)
                    if(entry.profile && entry.profile->game_biz==profile.game_biz)
                        return {LoadStatus::Ready,std::move(entry.authorization)};
                break;
            }
        }
    }
    if(readable) return {LoadStatus::Missing,std::nullopt};
    return {found ? LoadStatus::Unreadable : LoadStatus::Missing,std::nullopt};
}
bool Save(const std::string& directory,const GameProfile& profile,
          const Authorization& authorization) {
    std::lock_guard lock(store_mutex);
    const auto* supported=FindGameProfileByBiz(profile.game_biz);
    if(!supported || supported->app_id!=profile.app_id ||
       !Valid(authorization) || authorization.combo.app_id!=AppIdText(profile)) return false;
    Key key;
    if(!LoadKey(directory+"/cloudgenshin-login.key",key)) {
        std::string previous;
        if(Read(directory+"/cloudgenshin-login.vault",previous,kMaximum)!=ReadStatus::Missing ||
           Read(directory+"/cloudgenshin-login.vault.bak",previous,kMaximum)!=ReadStatus::Missing) return false;
        Random(key.bytes.data(),key.bytes.size());
        Secret encoded{std::string(reinterpret_cast<const char*>(key.bytes.data()),key.bytes.size())};
        if(!Write(directory+"/cloudgenshin-login.key",encoded.bytes)) return false;
    }
    // Preserve entries of every other game that can still be read; an
    // unreadable vault is replaced exactly like the single-entry format was.
    std::vector<Entry> entries;
    for(const auto& path:{directory+"/cloudgenshin-login.vault",directory+"/cloudgenshin-login.vault.bak"}) {
        std::string encoded;const auto read=Read(path,encoded,kMaximum);
        if(read==ReadStatus::Missing) continue;
        if(read!=ReadStatus::Ok) continue;
        for(const auto& key_path:{directory+"/cloudgenshin-login.key",directory+"/cloudgenshin-login.key.bak"}) {
            Key existing;if(!ReadKey(key_path,existing)) continue;
            auto decoded=DecryptEntries(encoded,existing);
            if(!decoded) continue;
            for(auto& entry:*decoded) {
                bool duplicate=false;
                for(const auto& kept:entries)
                    if(kept.game_biz()==entry.game_biz()) {duplicate=true;break;}
                if(!duplicate) entries.push_back(std::move(entry));
            }
            break;
        }
        if(!entries.empty()) break;
    }
    bool replaced=false;
    for(auto& entry:entries)
        if(entry.profile && entry.profile->game_biz==profile.game_biz) {
            entry.profile=&profile;entry.authorization=authorization;replaced=true;break;
        }
    if(!replaced) entries.push_back(Entry{&profile,authorization});
    Secret plaintext{Serialize(entries)};
    const auto encoded=Encrypt(plaintext.bytes,key);
    if(!encoded || encoded->size()>kMaximum || !DecryptEntries(*encoded,key)) return false;
    if(!Write(directory+"/cloudgenshin-login.vault",*encoded)) return false;
    return Remove(directory+"/cloudgenshin-login.disabled");
}
bool Clear(const std::string& directory) {
    std::lock_guard lock(store_mutex);
    // A completed logout must not revive a backup after an interrupted delete.
    const bool disabled=Write(directory+"/cloudgenshin-login.disabled","disabled");
    bool removed=true;
    for(const auto name:{"cloudgenshin-login.vault","cloudgenshin-login.key"})
        for(const auto suffix:{"",".tmp",".bak"}) {
            const bool ok=Remove(directory+"/"+name+suffix);removed=ok && removed;
        }
    return disabled && removed;
}
}
