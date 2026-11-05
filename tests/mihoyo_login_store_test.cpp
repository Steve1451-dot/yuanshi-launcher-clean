#include "mihoyo/login_store.hpp"
#include "mihoyo/production_profile.hpp"
#include <borealis/extern/nlohmann/json.hpp>
#include <mbedtls/gcm.h>
#include <array>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

static std::string read(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
}
// Rebuild the version-1 vault byte layout (header, nonce slot, tag, AAD) so an
// SD card written by the pre-multi-game build is exercised end to end.
static std::string legacy_v1_vault(const std::string& json,const std::string& key) {
    constexpr char kHeader[]="CGLGN001";
    constexpr char kAad[]="CloudGenshin login store v1";
    const std::array<unsigned char,12> nonce{1,2,3,4,5,6,7,8,9,10,11,12};
    std::array<unsigned char,16> tag{};
    std::string ciphertext(json.size(),'\0');
    mbedtls_gcm_context context;mbedtls_gcm_init(&context);
    int code=mbedtls_gcm_setkey(&context,MBEDTLS_CIPHER_ID_AES,
        reinterpret_cast<const unsigned char*>(key.data()),256);
    if(!code) code=mbedtls_gcm_crypt_and_tag(&context,MBEDTLS_GCM_ENCRYPT,json.size(),
        nonce.data(),nonce.size(),reinterpret_cast<const unsigned char*>(kAad),sizeof(kAad)-1,
        reinterpret_cast<const unsigned char*>(json.data()),
        reinterpret_cast<unsigned char*>(ciphertext.data()),tag.size(),tag.data());
    mbedtls_gcm_free(&context);assert(code==0);
    return std::string(kHeader,sizeof(kHeader)-1)+
        std::string(reinterpret_cast<const char*>(nonce.data()),nonce.size())+
        std::string(reinterpret_cast<const char*>(tag.data()),tag.size())+ciphertext;
}
int main() {
    using namespace cloudgenshin;
    using namespace cloudgenshin::login_store;
    const auto directory=std::filesystem::temp_directory_path()/
        ("cg-login-store-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto vault=directory/"cloudgenshin-login.vault",key=directory/"cloudgenshin-login.key";
    const auto& hk4e=DefaultGameProfile();
    const auto& sr=*FindGameProfile("hkrpg");
    const Authorization authorization{{"4","1","synthetic-open","synthetic-combo-token"},
        NativeWebIdentity(hk4e,"synthetic-device"),"123",{}};
    const Authorization star{{"8","1","synthetic-sr-open","synthetic-sr-token"},
        NativeWebIdentity(sr,"synthetic-sr-device"),"456",{}};
    assert(Load(directory.string(),hk4e).status==LoadStatus::Missing);
    assert(Save(directory.string(),hk4e,authorization));
    auto loaded=Load(directory.string(),hk4e);
    assert(loaded.status==LoadStatus::Ready && loaded.authorization);
    assert(loaded.authorization->combo.combo_token==authorization.combo.combo_token &&
        loaded.authorization->account_id=="123" && loaded.authorization->device.device_id=="synthetic-device" &&
        loaded.authorization->device.op_biz=="clgm_cn");
    auto full=authorization;
    full.channel_token="synthetic-passport-token";
    full.passport_mid="synthetic-passport-mid";
    full.combo.sdk_login_data=R"({"combo_id":9223372036854775000,"account_type":1,"heartbeat":true,"asterisk_name":"测***试","guest":false})";
    assert(Save(directory.string(),hk4e,full));
    loaded=Load(directory.string(),hk4e);
    assert(loaded.authorization && loaded.authorization->channel_token==full.channel_token);
    assert(loaded.authorization->passport_mid==full.passport_mid);
    assert(nlohmann::json::parse(loaded.authorization->combo.sdk_login_data)==
           nlohmann::json::parse(full.combo.sdk_login_data));
    assert(read(vault).find("synthetic-passport-token")==std::string::npos);
    assert(read(vault).find("synthetic-passport-mid")==std::string::npos);
    assert(read(vault).find("asterisk_name")==std::string::npos);
    const auto before_invalid=read(vault);
    auto invalid_mid=full;invalid_mid.passport_mid="bad;mid";
    assert(!Save(directory.string(),hk4e,invalid_mid) && read(vault)==before_invalid);
    invalid_mid=full;invalid_mid.channel_token="unsafe\nvalue";
    assert(!Save(directory.string(),hk4e,invalid_mid) && read(vault)==before_invalid);
    auto invalid_sdk=full; invalid_sdk.combo.sdk_login_data=R"({"channel_token":"not-allowed"})";
    assert(!Save(directory.string(),hk4e,invalid_sdk) && read(vault)==before_invalid);
    invalid_sdk.combo.sdk_login_data="[]";
    assert(!Save(directory.string(),hk4e,invalid_sdk) && read(vault)==before_invalid);
    assert(Save(directory.string(),hk4e,authorization));
    // A game without a saved entry reads as Missing while the vault is readable.
    assert(Load(directory.string(),sr).status==LoadStatus::Missing);
    const auto original=read(vault);assert(original.find("synthetic-combo-token")==original.npos);
    assert(read(key).size()==32);
    const auto original_key=read(key);
    std::filesystem::copy_file(key,key.string()+".bak");
    {std::ofstream damaged(key,std::ios::binary);const std::string wrong(32,'\0');damaged.write(wrong.data(),wrong.size());}
    assert(Load(directory.string(),hk4e).status==LoadStatus::Ready);
    std::filesystem::remove(key);std::filesystem::rename(key.string()+".bak",key);
    assert(Save(directory.string(),hk4e,authorization));
    assert(read(vault)!=original); // nonce differs on every save
    // A version-1 vault (single hk4e login) must keep working and migrate in
    // place while another game is added: neither entry may be lost.
    {const std::string legacy=nlohmann::json{{"version",1},{"app_id","4"},
        {"channel_id","1"},{"open_id","synthetic-open"},{"combo_token","legacy-combo-token"},
        {"account_id","123"},{"device_id","synthetic-device"},
        {"channel_token","synthetic-old-account-token"},{"passport_mid","invalid;context"}}.dump();
     std::ofstream file(vault,std::ios::binary);file<<legacy_v1_vault(legacy,original_key);}
    auto migrated=Load(directory.string(),hk4e);
    assert(migrated.status==LoadStatus::Ready &&
        migrated.authorization->combo.combo_token=="legacy-combo-token");
    assert(migrated.authorization->passport_mid.empty());
    assert(Load(directory.string(),sr).status==LoadStatus::Missing);
    // Removing a game from the catalog must not make a mixed old vault
    // unreadable or erase its opaque entry when another game is saved.
    const auto retired=nlohmann::json{{"game","nap_cn"},{"app_id","12"},{"channel_id","1"},
        {"open_id","retired-open"},{"combo_token","retired-token"},{"account_id","789"},
        {"device_id","retired-device"},{"opaque_extension",{{"preserve",true}}}};
    const auto retained=nlohmann::json{{"game","hk4e_cn"},{"app_id","4"},{"channel_id","1"},
        {"open_id","synthetic-open"},{"combo_token","legacy-combo-token"},{"account_id","123"},
        {"device_id","synthetic-device"}};
    {const auto mixed=nlohmann::json{{"version",2},{"entries",nlohmann::json::array({retired,retained})}}.dump();
     std::ofstream file(vault,std::ios::binary);file<<legacy_v1_vault(mixed,original_key);}
    assert(Load(directory.string(),hk4e).status==LoadStatus::Ready);
    assert(Save(directory.string(),sr,star));
    assert(Load(directory.string(),hk4e).status==LoadStatus::Ready && Load(directory.string(),sr).status==LoadStatus::Ready);
    {const auto encoded=read(vault);std::string plaintext(encoded.size()-36,'\0');
     mbedtls_gcm_context ctx;mbedtls_gcm_init(&ctx);
     assert(mbedtls_gcm_setkey(&ctx,MBEDTLS_CIPHER_ID_AES,reinterpret_cast<const unsigned char*>(original_key.data()),256)==0);
     constexpr char aad[]="CloudGenshin login store v1";
     assert(mbedtls_gcm_auth_decrypt(&ctx,plaintext.size(),reinterpret_cast<const unsigned char*>(encoded.data()+8),12,
         reinterpret_cast<const unsigned char*>(aad),sizeof(aad)-1,reinterpret_cast<const unsigned char*>(encoded.data()+20),16,
         reinterpret_cast<const unsigned char*>(encoded.data()+36),reinterpret_cast<unsigned char*>(plaintext.data()))==0);
     mbedtls_gcm_free(&ctx);
     const auto decoded=nlohmann::json::parse(plaintext);bool preserved=false;
     for(const auto& entry:decoded["entries"]) if(entry==retired) preserved=true;
     assert(preserved && decoded["entries"].size()==3);}
    {auto removed=hk4e;removed.game_biz="nap_cn";removed.app_id=12;
     auto removed_auth=authorization;removed_auth.combo.app_id="12";
     const auto before=read(vault);assert(!Save(directory.string(),removed,removed_auth) && read(vault)==before);}
    assert(Load(directory.string(),sr).status==LoadStatus::Ready);
    assert(Save(directory.string(),sr,star));
    auto migrated_sr=Load(directory.string(),sr);
    assert(migrated_sr.status==LoadStatus::Ready &&
        migrated_sr.authorization->device.op_biz=="clgm_hkrpg-cn" &&
        migrated_sr.authorization->combo.combo_token=="synthetic-sr-token");
    migrated=Load(directory.string(),hk4e);
    assert(migrated.status==LoadStatus::Ready &&
        migrated.authorization->combo.combo_token=="legacy-combo-token");
    std::filesystem::rename(vault,vault.string()+".bak");
    assert(Load(directory.string(),hk4e).status==LoadStatus::Ready);
    // A corrupt primary must recover the last complete authenticated backup.
    {std::ofstream damaged(vault,std::ios::binary);damaged<<"broken";}
    assert(Load(directory.string(),hk4e).status==LoadStatus::Ready);
    std::filesystem::remove(vault.string()+".bak");
    assert(Load(directory.string(),hk4e).status==LoadStatus::Unreadable);
    assert(Save(directory.string(),hk4e,authorization));
    {auto bytes=read(vault);bytes.back()^=1;std::ofstream modified(vault,std::ios::binary);modified.write(bytes.data(),bytes.size());}
    assert(Load(directory.string(),hk4e).status==LoadStatus::Unreadable);
    assert(Clear(directory.string()));
    assert(!std::filesystem::exists(vault) && !std::filesystem::exists(key));
    assert(Load(directory.string(),hk4e).status==LoadStatus::Missing);
    // Even if an old backup remains, the logout marker prevents restoration.
    {std::ofstream backup(vault.string()+".bak",std::ios::binary);backup.write(original.data(),original.size());}
    {std::ofstream backup_key(key.string()+".bak",std::ios::binary);backup_key.write(original_key.data(),original_key.size());}
    assert(Load(directory.string(),hk4e).status==LoadStatus::Missing);
    assert(Clear(directory.string()));
    assert(Save(directory.string(),hk4e,authorization));
    assert(Load(directory.string(),hk4e).status==LoadStatus::Ready);
    // Multi-game: saving one game must not drop the other entry, and an
    // app_id that does not belong to the selected game is refused.
    assert(Save(directory.string(),sr,star));
    auto sr_entry=Load(directory.string(),sr);
    assert(sr_entry.status==LoadStatus::Ready && sr_entry.authorization &&
        sr_entry.authorization->combo.combo_token=="synthetic-sr-token");
    auto hk_entry=Load(directory.string(),hk4e);
    assert(hk_entry.status==LoadStatus::Ready && hk_entry.authorization &&
        hk_entry.authorization->combo.combo_token=="synthetic-combo-token");
    {auto wrong=authorization;wrong.combo.app_id="8";
     assert(!Save(directory.string(),hk4e,wrong));}
    auto invalid=authorization;invalid.combo.combo_token="invalid\nheader";
    const auto previous=read(vault);assert(!Save(directory.string(),hk4e,invalid));assert(read(vault)==previous);
    const auto current_key=read(key);assert(current_key!=original_key); // new login after logout rotates the key
    {std::ofstream damaged(key,std::ios::binary);damaged<<"broken";}
    assert(Load(directory.string(),hk4e).status==LoadStatus::Unreadable);
    assert(!Save(directory.string(),hk4e,authorization));assert(read(vault)==previous);
    {std::ofstream repaired(key,std::ios::binary);repaired.write(current_key.data(),current_key.size());}
    assert(Load(directory.string(),hk4e).status==LoadStatus::Ready);
    assert(!Save((directory/"missing").string(),hk4e,authorization));
    assert(Clear(directory.string()));
    std::filesystem::remove(directory/"cloudgenshin-login.disabled");std::filesystem::remove(directory);
}
