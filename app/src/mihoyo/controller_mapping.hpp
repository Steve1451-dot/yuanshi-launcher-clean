#pragma once
#include "mihoyo/input_packet.hpp"
#include "atomic_file_replace.hpp"
#include <array>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace cloudgenshin::input {
// Source/target indices name Switch HID fields. Only a COPY of remote gameplay
// input is transformed; local navigation, capture and synthetic entry use raw input.
inline constexpr std::array<bool SwitchControllerState::*,16> kMappedButtons{
    &SwitchControllerState::a,&SwitchControllerState::b,&SwitchControllerState::x,&SwitchControllerState::y,
    &SwitchControllerState::l,&SwitchControllerState::r,&SwitchControllerState::zl,&SwitchControllerState::zr,
    &SwitchControllerState::stick_left,&SwitchControllerState::stick_right,
    &SwitchControllerState::plus,&SwitchControllerState::minus,
    &SwitchControllerState::dpad_up,&SwitchControllerState::dpad_down,
    &SwitchControllerState::dpad_left,&SwitchControllerState::dpad_right};
inline constexpr unsigned kDisabledButton=16,kFixedMinusButton=11;
inline constexpr unsigned kLocalMinusButton=kFixedMinusButton; // Legacy source alias; now game Back/map.
// Switch field indices, BEFORE Xbox encoding. R -> Xbox B (attack),
// right A -> Xbox RB (dash); bottom B -> A, top X -> Y, left Y -> X.
inline constexpr std::array<unsigned,16> kGenshinPositionTargets{
    5,1,2,3,4,0,6,7,8,9,10,11,12,13,14,15};
struct ControllerMapping {
    std::array<unsigned,16> target{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
    // Local rate-to-stick assistance, not native IMU. Genshin defaults on.
    bool gyro_assist=false;
    unsigned gyro_sensitivity=100; // percent; 50,100,150,200
    bool gyro_aim_only=true,gyro_invert_y=false,gyro_invert_x=false;
    bool operator==(const ControllerMapping&) const = default;
};
inline std::string_view MappingProfileForApp(std::string_view app) {
    return app=="4" ? "hk4e" : app=="8" ? "hkrpg" : "";
}
inline bool KnownMappingProfile(std::string_view profile) {
    return profile=="hk4e" || profile=="hkrpg" || profile=="endfield";
}
inline ControllerMapping DefaultControllerMapping(std::string_view profile) {
    ControllerMapping map;
    if(profile=="hk4e") {
        map.target=kGenshinPositionTargets;
        map.gyro_assist=true;map.gyro_aim_only=true;map.gyro_invert_x=true;
    }
    return map;
}
inline bool ValidControllerMapping(const ControllerMapping& map,std::string_view profile) {
    if(!KnownMappingProfile(profile) || (profile!="hk4e" && map.gyro_assist)) return false;
    for(auto target:map.target) if(target>kDisabledButton) return false;
    return map.target[kFixedMinusButton]==kFixedMinusButton &&
        (map.gyro_sensitivity==50 || map.gyro_sensitivity==100 || map.gyro_sensitivity==150 || map.gyro_sensitivity==200);
}
inline SwitchControllerState ApplyControllerMapping(const ControllerMapping& map,const SwitchControllerState& raw) {
    SwitchControllerState out=raw;
    for(auto target:map.target) if(target>kDisabledButton) return raw;
    for(auto member:kMappedButtons) out.*member=false;
    for(unsigned i=0;i<kMappedButtons.size();++i)
        if(raw.*kMappedButtons[i] && map.target[i]!=kDisabledButton) out.*kMappedButtons[map.target[i]]=true;
    return out; // Duplicate targets are ORed; releasing one source cannot erase another.
}
inline bool HasGenshinPositionPreset(const ControllerMapping& map) {
    return map.target==kGenshinPositionTargets;
}
struct GameplayMappingFrame {
    SwitchControllerState state;
    bool gyro_allowed=true,wheel_navigation=false,native_navigation=false;
};
// Explicit native passthrough: never guess whether the pod is displaying a menu.
// L is a physical modifier. Restore the shoulder/right-face pair while held:
// keep LB+B on L+right-A, without discarding unrelated custom D-pad/buttons.
inline GameplayMappingFrame ComposeGameplayMapping(std::string_view profile,
        const ControllerMapping& map,const SwitchControllerState& raw,bool native_navigation=false) {
    const bool genshin=profile=="hk4e";
    const bool navigation=genshin && native_navigation;
    const bool wheel=genshin && !navigation && map.target[0]==5 && map.target[5]==0 && map.target[4]==4 && raw.l;
    auto effective=map;
    if(wheel) {effective.target[0]=0;effective.target[5]=5;}
    return {navigation ? raw : ApplyControllerMapping(effective,raw),
        !(genshin && (navigation || raw.l)),wheel,navigation};
}
inline bool UsesShoulderMenuChord(std::string_view profile) {
    return profile!="hk4e"; // Genshin L/R belong to the game; use the touch ball.
}
inline const char* PhysicalButtonName(unsigned i) {
    constexpr const char* names[]{"A","B","X","Y","L","R","ZL","ZR","L3","R3","+","-","D-pad up","D-pad down","D-pad left","D-pad right","Disabled"};
    return names[i<=kDisabledButton?i:kDisabledButton];
}
inline const char* RemoteButtonName(unsigned i,std::string_view layout="Xbox") {
    if(i<4 && layout!="Switch") {constexpr unsigned face[]{1,0,3,2};return PhysicalButtonName(face[i]);}
    constexpr const char* names[]{"A","B","X","Y","LB","RB","LT","RT","L3","R3","Start","Back","D-pad up","D-pad down","D-pad left","D-pad right","Disabled"};
    return names[i<=kDisabledButton?i:kDisabledButton];
}
// Descriptions assume the cloud game's original Xbox bindings, never a claim
// that a saved remote action table has been read or changed.
inline const char* GenshinDefaultActionName(unsigned target) {
    constexpr const char* names[]{"Normal attack","Jump","Elemental burst","Interact",
        "Shortcut wheel","Dash","Aim","Elemental skill"};
    return target<8?names[target]:"";
}
inline std::string ControllerMappingPath(const std::string& home,std::string_view profile) {
    return KnownMappingProfile(profile)?home+"/controller-mapping-"+std::string(profile)+".txt":std::string{};
}
struct LoadedControllerMapping {ControllerMapping value;bool restored=false,upgraded=false;};
inline LoadedControllerMapping LoadControllerMapping(const std::string& home,std::string_view profile) {
    const auto path=ControllerMappingPath(home,profile);
    if(!path.empty()) for(const auto& file:{path,path+".bak"}) {
        std::ifstream in(file,std::ios::binary);
        if(!in) continue;
        in.seekg(0,std::ios::end);const auto size=in.tellg();
        if(size<0 || size>256) continue;
        in.seekg(0);
        std::string magic,key;int version=0,gyro=-1,aim=-1,invert=-1,invert_x=-1;
        ControllerMapping map;
        if(!(in>>magic>>version>>key) || magic!="CGNX-PAD" || version<1 || version>4 || key!=profile) continue;
        bool parsed=true;for(auto& target:map.target) if(!(in>>target)) {parsed=false;break;}
        if(!parsed || !(in>>gyro>>map.gyro_sensitivity>>aim>>invert>>invert_x) ||
            gyro<0 || gyro>1 || aim<0 || aim>1 || invert<0 || invert>1 || invert_x<0 || invert_x>1) continue;
        map.gyro_assist=gyro;map.gyro_aim_only=aim;map.gyro_invert_y=invert;map.gyro_invert_x=invert_x;
        in>>std::ws;
        if(in.eof() && ValidControllerMapping(map,profile)) {
            // Upgrade the previous default-off/native experiment once in memory.
            // Keep all button targets, sensitivity and inversion preferences.
            // Saving the current format keeps later explicit Off/always-on choices.
            if(version==1 && profile=="hk4e") {map.gyro_assist=true;map.gyro_aim_only=true;}
            // Physical feedback confirmed old horizontal aim was reversed.
            // Normalize old settings to the corrected direction, not XOR:
            // a user who already enabled X inversion must not be flipped twice.
            // Version 3 keeps all subsequent explicit direction choices.
            if(version<3 && profile=="hk4e") map.gyro_invert_x=true;
            // Only migrate known old defaults, never overwrite a custom table.
            // v4 marks an explicit choice, including choosing the old layout.
            if(version<4 && profile=="hk4e") {
                const std::array<unsigned,16> old_default{1,0,3,2,4,5,6,7,8,9,10,11,12,13,14,15};
                if(map.target==old_default || map.target==ControllerMapping{}.target)
                    map.target=kGenshinPositionTargets;
            }
            // Auto-save only a valid primary. A corrupt primary must not be
            // rotated over the sole good .bak merely to mark a migration.
            return {map,true,version<4 && profile=="hk4e" && file==path};
        }
    }
    return {DefaultControllerMapping(profile),false};
}
inline bool SaveControllerMapping(const std::string& home,std::string_view profile,const ControllerMapping& map) {
    if(!ValidControllerMapping(map,profile)) return false;
    const auto path=ControllerMappingPath(home,profile);
    std::ofstream out(path+".tmp",std::ios::binary|std::ios::trunc);
    if(!out) return false;
    out<<"CGNX-PAD 4 "<<profile<<'\n';
    for(auto target:map.target) out<<target<<' ';
    out<<'\n'<<map.gyro_assist<<' '<<map.gyro_sensitivity<<' '<<map.gyro_aim_only<<' '<<map.gyro_invert_y<<' '<<map.gyro_invert_x<<'\n';
    out.flush();const bool good=out.good();out.close();
    return good && !out.fail() && opennow::storage::ReplaceWithTemporaryFile(path+".tmp",path);
}
} // namespace cloudgenshin::input
