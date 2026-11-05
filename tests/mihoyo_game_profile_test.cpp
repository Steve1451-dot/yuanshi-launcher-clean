#include "mihoyo/production_profile.hpp"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

int main() {
    using namespace cloudgenshin;
    constexpr std::size_t profiles=sizeof(kGameProfiles)/sizeof(kGameProfiles[0]);
    static_assert(profiles==2);
    assert(&DefaultGameProfile()==&kGameProfiles[0]);
    assert(FindGameProfile("hkrpg")==&kGameProfiles[1]);
    assert(FindGameProfile("nap")==nullptr); // Removed game cannot be selected.
    assert(FindGameProfile("missing")==nullptr);
    assert(FindGameProfileByBiz("hk4e_cn")==&kGameProfiles[0]);
    assert(FindGameProfileByBiz("nap_cn")==nullptr);
    assert(kGameProfiles[0].app_id==4 && kGameProfiles[1].app_id==8);
    for(std::size_t index=0;index<profiles;++index) {
        const auto& profile=kGameProfiles[index];
        assert(!profile.id.empty() && !profile.game_biz.empty() && !profile.op_biz.empty());
        assert(!profile.display_name.empty() && !profile.latin_name.empty());
        assert(!profile.sdk_version.empty() && !profile.app_version.empty());
        assert(std::string(profile.cloud_base).starts_with("https://"));
        assert(std::string(profile.sdk_base).starts_with("https://"));
        assert(std::string(profile.game_biz).find('_')!=std::string::npos);
        // Signing values are lowercase hex application keys, never token shapes.
        for(const auto* key:{&profile.combo_app_key,&profile.dispatch_app_key}) {
            assert(key->size()==32);
            for(const char c:*key) assert((c>='0' && c<='9')||(c>='a' && c<='f'));
        }
        // Persistence keys, game identities and app ids must stay unique.
        for(std::size_t other=0;other<index;++other) {
            assert(kGameProfiles[other].id!=profile.id);
            assert(kGameProfiles[other].game_biz!=profile.game_biz);
            assert(kGameProfiles[other].app_id!=profile.app_id);
        }
    }
    assert(AppIdText(kGameProfiles[0])=="4" && AppIdText(kGameProfiles[1])=="8");
    const auto sr_identity=NativeWebIdentity(kGameProfiles[1],"synthetic");
    assert(sr_identity.op_biz=="clgm_hkrpg-cn" && sr_identity.app_version=="4.6.0" &&
        sr_identity.client_type=="19" && sr_identity.cps.empty());

    // Selection persistence round-trip, junk fallback and .bak fallback.
    const auto directory=std::filesystem::temp_directory_path()/
        ("cg-game-select-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto file=(directory/"cloudgenshin-game.txt").string();
    assert(&LoadGameSelection(file)==&kGameProfiles[0]); // missing file: default game
    assert(SaveGameSelection(file,kGameProfiles[1]));
    assert(&LoadGameSelection(file)==&kGameProfiles[1]);
    {std::ofstream junk(file,std::ios::binary|std::ios::trunc);junk<<"junk\n";}
    assert(&LoadGameSelection(file)==&kGameProfiles[0]); // unknown value: default game
    assert(SaveGameSelection(file,kGameProfiles[1]));
    {std::ofstream newline(file,std::ios::binary|std::ios::trunc);newline<<"nap\n";}
    assert(&LoadGameSelection(file)==&kGameProfiles[0]); // Removed selection: safe fallback.
    {std::ofstream newline(file,std::ios::binary|std::ios::trunc);newline<<"hkrpg\n";}
    assert(&LoadGameSelection(file)==&kGameProfiles[1]); // trailing newline tolerated
    // Simulate an interrupted update where only the automatic .bak survived.
    assert(SaveGameSelection(file,kGameProfiles[1]));
    std::filesystem::remove(file+".bak");
    std::filesystem::rename(file,file+".bak");
    assert(&LoadGameSelection(file)==&kGameProfiles[1]);
    assert(!SaveGameSelection((directory/"missing"/"cloudgenshin-game.txt").string(),kGameProfiles[1]));
    std::filesystem::remove(file+".bak");
    std::filesystem::remove(directory);
}
