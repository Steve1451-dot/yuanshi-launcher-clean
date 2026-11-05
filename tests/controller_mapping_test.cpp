#include "mihoyo/controller_mapping.hpp"
#include "mihoyo/gameplay_button_policy.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>

using namespace cloudgenshin::input;
int main() {
    const auto original=DefaultControllerMapping("endfield"),genshin=DefaultControllerMapping("hk4e");
    for(unsigned bits=0;bits<65536;++bits) {
        SwitchControllerState raw;
        for(unsigned i=0;i<16;++i) raw.*kMappedButtons[i]=(bits&(1u<<i))!=0;
        raw.left_x=-12345;raw.left_y=22222;raw.right_x=-32767;raw.right_y=6000;
        const auto before=ToGamepadState(raw,123);
        for(const auto profile:{"endfield","hkrpg"}) {
            const auto mapped=ToGamepadState(ApplyControllerMapping(DefaultControllerMapping(profile),raw),123);
            assert(EncodeGamepadChannel(before)==EncodeGamepadChannel(mapped));
        }
        const auto mapped=ApplyControllerMapping(genshin,raw);
        const auto expected=(before.buttons&~(0x2000u|0x0200u)) |
            ((before.buttons&0x2000u)?0x0200u:0u) | ((before.buttons&0x0200u)?0x2000u:0u);
        assert(ToGamepadState(mapped,123).buttons==expected);
        const auto combat=ComposeGameplayMapping("hk4e",genshin,raw);
        assert(combat.wheel_navigation==raw.l && combat.gyro_allowed==!raw.l && !combat.native_navigation);
        assert(ToGamepadState(combat.state,123).buttons==(raw.l?before.buttons:expected));
        const auto navigation=ComposeGameplayMapping("hk4e",genshin,raw,true);
        assert(navigation.native_navigation && !navigation.gyro_allowed);
        assert(EncodeGamepadChannel(ToGamepadState(navigation.state,123))==EncodeGamepadChannel(before));
        for(const auto profile:{"endfield","hkrpg"}) {
            const auto frame=ComposeGameplayMapping(profile,original,raw,true);
            assert(frame.gyro_allowed && !frame.native_navigation && !frame.wheel_navigation);
            assert(EncodeGamepadChannel(ToGamepadState(frame.state,123))==EncodeGamepadChannel(before));
        }
        assert(mapped.left_x==raw.left_x && mapped.left_y==raw.left_y &&
               mapped.right_x==raw.right_x && mapped.right_y==raw.right_y);
    }
    for(unsigned source=0;source<16;++source) for(unsigned target=0;target<=16;++target) {
        auto map=original;map.target[source]=target;
        SwitchControllerState raw;raw.*kMappedButtons[source]=true;
        const auto out=ApplyControllerMapping(map,raw);
        for(unsigned i=0;i<16;++i) assert(out.*kMappedButtons[i]==(i==target));
        assert(raw.*kMappedButtons[source]); // Never mutate local input.
    }
    auto duplicate=original;duplicate.target[0]=duplicate.target[1]=6;
    SwitchControllerState both;both.a=both.b=true;
    assert(ApplyControllerMapping(duplicate,both).zl);
    both.a=false;assert(ApplyControllerMapping(duplicate,both).zl);
    both.b=false;assert(!ApplyControllerMapping(duplicate,both).zl);
    auto invalid=original;invalid.target[0]=999;
    both.x=true;assert(ApplyControllerMapping(invalid,both).x);
    assert(std::string(RemoteButtonName(1,"Xbox"))=="A" && std::string(RemoteButtonName(1,"Switch"))=="B");
    assert(MappingProfileForApp("4")=="hk4e" && MappingProfileForApp("8")=="hkrpg" &&
           MappingProfileForApp("12").empty() && MappingProfileForApp("anything").empty());
    assert(!KnownMappingProfile("nap"));
    const auto dir=std::filesystem::temp_directory_path()/
        ("cgnx-mapping-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    const auto home=dir.string();
    assert(!LoadControllerMapping(home,"hk4e").restored);
    auto custom=genshin;custom.target[4]=7;custom.target[0]=16;custom.gyro_assist=true;
    custom.gyro_sensitivity=150;custom.gyro_aim_only=false;custom.gyro_invert_y=true;
    for(const auto profile:{"hk4e","hkrpg","endfield"}) {
        const auto value=std::string_view(profile)=="hk4e"?custom:original;
        assert(SaveControllerMapping(home,profile,value));
        const auto loaded=LoadControllerMapping(home,profile);
        assert(loaded.restored && loaded.value==value);
    }
    const auto path=ControllerMappingPath(home,"hk4e");
    std::filesystem::copy_file(path,path+".bak");
    for(const auto& bad:{std::string("CGNX-PAD 5 hk4e"),std::string(400,'x'),
        std::string("CGNX-PAD 1 endfield 0 1 2"),std::string("CGNX-PAD 1 hk4e -1"),
        std::string("CGNX-PAD 1 hk4e 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 1 100 1 0 0 junk")}) {
        {std::ofstream out(path);out<<bad;}
        const auto recovered=LoadControllerMapping(home,"hk4e");
        assert(recovered.restored && recovered.value==custom);
    }
    std::filesystem::remove(path+".bak");
    assert(!LoadControllerMapping(home,"hk4e").restored && LoadControllerMapping(home,"hk4e").value==genshin);
    assert(SaveControllerMapping(home,"hk4e",custom));
    auto unsafe=custom;unsafe.target[11]=0;
    assert(!SaveControllerMapping(home,"hk4e",unsafe));
    assert(!SaveControllerMapping(home,"endfield",custom));
    assert(!SaveControllerMapping(home,"../escape",original));
    assert(ControllerMappingPath(home,"../escape").empty());
    assert(!SaveControllerMapping(home+"/missing","hk4e",custom));
    assert(LoadControllerMapping(home,"hk4e").value==custom); // Failed writes don't erase good data.
    // Old v1 native/PS5 defaults upgrade without losing custom button bindings.
    {std::ofstream out(path);out<<"CGNX-PAD 1 hk4e\n";
     for(auto target:custom.target) out<<target<<' ';
     out<<"\n0 150 0 1 1\n";}
    auto migrated=LoadControllerMapping(home,"hk4e").value;
    assert(migrated.gyro_assist && migrated.gyro_aim_only && migrated.target==custom.target &&
        migrated.gyro_sensitivity==150 && migrated.gyro_invert_x && migrated.gyro_invert_y);
    migrated.gyro_assist=false;
    assert(SaveControllerMapping(home,"hk4e",migrated));
    assert(!LoadControllerMapping(home,"hk4e").value.gyro_assist); // Explicit new Off persists.
    for(const int old_version:{1,2}) for(const bool already_corrected:{false,true}) {
        {std::ofstream out(path);out<<"CGNX-PAD "<<old_version<<" hk4e\n";
         for(auto target:custom.target) out<<target<<' ';
         out<<"\n0 150 0 1 "<<already_corrected<<"\n";}
        const auto upgraded=LoadControllerMapping(home,"hk4e").value;
        assert(upgraded.gyro_invert_x && upgraded.gyro_invert_y && upgraded.target==custom.target &&
            upgraded.gyro_sensitivity==150 && upgraded.gyro_assist==(old_version==1));
    }
    auto explicit_direction=custom;explicit_direction.gyro_invert_x=false;
    assert(SaveControllerMapping(home,"hk4e",explicit_direction));
    assert(LoadControllerMapping(home,"hk4e").value==explicit_direction);
    // Known old defaults migrate; all unrelated gyro choices survive. A v4
    // explicit legacy choice, and every actual custom table, stay unchanged.
    const std::array<unsigned,16> legacy{1,0,3,2,4,5,6,7,8,9,10,11,12,13,14,15};
    for(const int version:{1,2,3,4}) for(const auto& targets:{legacy,original.target,custom.target}) {
        {std::ofstream out(path);out<<"CGNX-PAD "<<version<<" hk4e\n";
         for(auto target:targets) out<<target<<' ';
         out<<"\n0 150 0 1 0\n";}
        const auto loaded=LoadControllerMapping(home,"hk4e");
        assert(loaded.restored && loaded.upgraded==(version<4));
        assert(loaded.value.target==((version<4 && targets!=custom.target)?genshin.target:targets));
        assert(loaded.value.gyro_sensitivity==150 && loaded.value.gyro_invert_y &&
            loaded.value.gyro_invert_x==(version<3) && loaded.value.gyro_assist==(version==1));
    }
    auto custom_frame=genshin;custom_frame.target[0]=16;
    SwitchControllerState wheel;wheel.l=true;wheel.r=true;
    assert(!ComposeGameplayMapping("hk4e",custom_frame,wheel).wheel_navigation);
    custom_frame=genshin;custom_frame.target[12]=13;wheel={};wheel.l=true;wheel.a=true;wheel.dpad_up=true;
    const auto custom_wheel=ComposeGameplayMapping("hk4e",custom_frame,wheel);
    assert(custom_wheel.wheel_navigation && custom_wheel.state.a && !custom_wheel.state.r &&
        !custom_wheel.state.dpad_up && custom_wheel.state.dpad_down);
    assert(!UsesShoulderMenuChord("hk4e") && UsesShoulderMenuChord("endfield") && UsesShoulderMenuChord("hkrpg"));
    assert(std::string(GenshinDefaultActionName(genshin.target[5]))=="Normal attack");
    assert(std::string(GenshinDefaultActionName(16)).empty());
    {std::ofstream out(path+".bak");out<<"CGNX-PAD 3 hk4e\n";
     for(auto target:legacy) out<<target<<' ';
     out<<"\n1 100 1 0 1\n";}
    {std::ofstream out(path);out<<"corrupt primary";}
    const auto backup_upgrade=LoadControllerMapping(home,"hk4e");
    assert(backup_upgrade.restored && !backup_upgrade.upgraded && backup_upgrade.value==genshin);
    for(const auto profile:{"hk4e","hkrpg","endfield"}) {
        const auto name=ControllerMappingPath(home,profile);
        for(const auto& suffix:{"",".tmp",".bak"}) std::filesystem::remove(name+suffix);
    }
    assert(std::filesystem::remove(dir)); // Remove only this empty, uniquely created test directory.
    std::cout<<"PASS per-game mapping: all button masks/targets, wire defaults, isolation, persistence, recovery\n";
}
