#include "mihoyo/stream_settings_store.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    using namespace cloudgenshin::overlay;
    const auto dir=std::filesystem::temp_directory_path()/
        ("cg-settings-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    const auto path=(dir/"cloudgenshin-stream-settings.txt").string();
    assert(std::filesystem::path(StreamSettingsPath(dir.string(),"hk4e")).lexically_normal()==
        std::filesystem::path(path).lexically_normal());
    const auto sr=StreamSettingsPath(dir.string(),"hkrpg");
    assert(!sr.empty() && sr!=path && StreamSettingsPath(dir.string(),"nap").empty());
    for(const auto profile:{"", "endfield", "../hk4e", "hkrpg/../hk4e"})
        assert(StreamSettingsPath(dir.string(),profile).empty());
    assert(!LoadSavedSettings("").valid && !SaveSavedSettings("",2,60));

    // Nothing saved yet: callers must fall back to the startup preset.
    assert(!LoadSavedSettings(path).valid);

    // Round trip.
    assert(SaveSavedSettings(path,1,60));
    const auto saved=LoadSavedSettings(path);
    assert(saved.valid && saved.graphics==1 && saved.fps==60);
    // A new Star Rail install uses its own low/60 fallback, not Genshin's saved
    // preference. Later changes never overwrite another game's primary/backup.
    assert(!LoadSavedSettings(sr).valid);
    assert(SaveSavedSettings(sr,2,30));
    assert(LoadSavedSettings(path).graphics==1 && LoadSavedSettings(path).fps==60);
    assert(LoadSavedSettings(sr).graphics==2 && LoadSavedSettings(sr).fps==30);

    // Invalid values are refused on write and never sent back on read.
    assert(!SaveSavedSettings(path,3,60));
    assert(!SaveSavedSettings(path,1,45));
    assert(LoadSavedSettings(path).graphics==1);

    // A damaged file is treated as absent, not as a bogus request.
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"garbage";}
    assert(!LoadSavedSettings(path).valid);

    // Out-of-range numbers in a well-formed file are rejected too.
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"9 60";}
    assert(!LoadSavedSettings(path).valid);
    {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<"2 120";}
    assert(!LoadSavedSettings(path).valid);

    // The previous good copy is used when the live file is gone.
    assert(SaveSavedSettings(path,2,30));
    std::filesystem::copy_file(path,path+".bak",std::filesystem::copy_options::overwrite_existing);
    std::filesystem::remove(path);
    const auto recovered=LoadSavedSettings(path);
    assert(recovered.valid && recovered.graphics==2 && recovered.fps==30);
    {std::ofstream out(path);out<<"0 30";}
    const auto migrated=LoadSavedSettings(path);
    assert(migrated.valid && migrated.graphics==2 && migrated.fps==60);
    assert(SaveSavedSettings(path,0,30));
    assert(LoadSavedSettings(path).graphics==0 && LoadSavedSettings(path).fps==30);
    for(const auto* invalid:{"0 30 bad", "0 30 CGNX-LOW60-V1 trailing"}) {
        std::filesystem::remove(path+".bak");std::ofstream out(path);out<<invalid;out.close();
        assert(!LoadSavedSettings(path).valid);
    }

    std::filesystem::remove_all(dir);
}
