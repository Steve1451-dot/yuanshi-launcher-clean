#include "mihoyo/genshin_input_experiment.hpp"
#include "mihoyo/controller_mapping.hpp"
#include "mihoyo/gyro_stick_assist.hpp"
#include "mihoyo/game_control.hpp"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <utility>
using namespace cloudgenshin;
int main() {
    using input::GenshinInputMode;
    const auto directory=std::filesystem::temp_directory_path()/
        ("cg-input-experiment-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto home=directory.string(),path=input::GenshinInputModePath(home);
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::Standard);
    assert(input::SaveGenshinInputMode(home,GenshinInputMode::AndroidPs5));
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::AndroidPs5);
    assert(input::SaveGenshinInputMode(home,GenshinInputMode::Standard));
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::Standard);
    assert(input::SaveGenshinInputMode(home,GenshinInputMode::WebPs5));
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::WebPs5);
    assert(input::SaveGenshinInputMode(home,GenshinInputMode::CurrentPs5));
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::CurrentPs5);
    assert(input::SaveGenshinInputMode(home,GenshinInputMode::Standard));
    assert(!input::SaveGenshinInputMode(home,static_cast<GenshinInputMode>(4)));
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::Standard);
    assert(!input::SaveGenshinInputMode((directory/"missing").string(),GenshinInputMode::AndroidPs5));
    // Interrupted rename recovery; corrupt main must not hide a good backup.
    assert(input::SaveGenshinInputMode(home,GenshinInputMode::AndroidPs5));
    std::filesystem::rename(path,path+".bak");
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::AndroidPs5);
    const auto write=[&](const std::string& value) {std::ofstream out(path,std::ios::binary|std::ios::trunc);out<<value;};
    for(const auto& bad:{std::string("CGNX-INPUT 2 android-ps5"),std::string("CGNX-INPUT 1 ps5"),
        std::string("CGNX-INPUT 1 android-ps5 trailing"),std::string(65,'x'),std::string("CGNX-INPUT 1 android-ps5\0",25)}) {
        write(bad);assert(input::LoadGenshinInputMode(home)==GenshinInputMode::AndroidPs5);
        std::filesystem::remove(path+".bak");
        assert(input::LoadGenshinInputMode(home)==GenshinInputMode::Standard);
        {std::ofstream backup(path+".bak");backup<<"CGNX-INPUT 1 android-ps5\n";}
    }
    write("CGNX-INPUT 1 standard\r\n"); // Explicit off beats an older enabled backup.
    assert(input::LoadGenshinInputMode(home)==GenshinInputMode::Standard);
    assert(input::AndroidPs5Experiment("hk4e",GenshinInputMode::AndroidPs5));
    for(const auto profile:{"hk4e","hkrpg","nap","endfield",""}) {
        assert(!input::AndroidPs5Experiment(profile,GenshinInputMode::Standard));
        if(std::string_view(profile)!="hk4e") assert(!input::AndroidPs5Experiment(profile,GenshinInputMode::AndroidPs5));
        for(const auto mode:{GenshinInputMode::Standard,GenshinInputMode::AndroidPs5,GenshinInputMode::WebPs5,GenshinInputMode::CurrentPs5}) {
            const auto scoped=input::ScopedGenshinInputMode(profile,mode);
            assert(scoped==(std::string_view(profile)=="hk4e"?mode:GenshinInputMode::Standard));
            assert(input::Ps5InputExperiment(scoped)==(std::string_view(profile)=="hk4e" && mode!=GenshinInputMode::Standard));
        }
    }
    for(unsigned terminal:{3U,5U,6U,7U,10U,19U,700U}) for(const auto vendor:{0x045eU,0x054cU,0x057eU}) {
        const input::SessionInputIdentity old{terminal,vendor,0x028e};
        assert(input::ResolveSessionInputIdentity(GenshinInputMode::Standard,old)==old);
        assert((input::ResolveSessionInputIdentity(GenshinInputMode::AndroidPs5,old)==input::SessionInputIdentity{3,0x054c,0x0ce6}));
        assert((input::ResolveSessionInputIdentity(GenshinInputMode::WebPs5,old)==input::SessionInputIdentity{10,0x054c,0x0ce6}));
        assert((input::ResolveSessionInputIdentity(GenshinInputMode::CurrentPs5,old)==input::SessionInputIdentity{terminal,0x054c,0x0ce6}));
    }
    assert(static_cast<int>(GenshinInputMode::CurrentPs5)==3);
    assert(std::string_view(input::GenshinInputModeEvent(GenshinInputMode::CurrentPs5))=="GENSHIN_CURRENT_PS5_EXPERIMENT_ACTIVE");
    // Saving input preferences must not migrate existing terminal, pad override,
    // game selection or decoder preferences (including their recovery backups).
    const std::pair<const char*,const char*> preferences[]={
        {"cloudgenshin-terminal.txt","6"},{"cloudgenshin-terminal.txt.bak","7"},
        {"cloudgenshin-pad-id.txt","xbox\n"},{"cloudgenshin-game.txt","hk4e\n"},
        {"cloudgenshin-decoder.txt","hardware\n"},{"cloudgenshin-decoder.txt.bak","software\n"}};
    for(const auto& [name,value]:preferences) {std::ofstream out(directory/name,std::ios::binary);out<<value;}
    for(const auto mode:{GenshinInputMode::CurrentPs5,GenshinInputMode::Standard}) {
        assert(input::SaveGenshinInputMode(home,mode) && input::LoadGenshinInputMode(home)==mode);
        for(const auto& [name,value]:preferences) {
            std::ifstream in(directory/name,std::ios::binary);
            assert(std::string(std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>())==value);
        }
    }
    assert(input::UseLocalGyroAssist(false,true));
    assert(!input::UseLocalGyroAssist(false,false) && !input::UseLocalGyroAssist(true,true));
    // Existing per-game mapping is untouched. The report stays APK XinputData,
    // changing only VID/PID; PS identity must not change semantic button bits.
    auto map=input::DefaultControllerMapping("hk4e");map.gyro_assist=true;
    assert(input::SaveControllerMapping(home,"hk4e",map));
    const auto effective=input::ResolveSessionInputIdentity(GenshinInputMode::CurrentPs5,{6,0x045e,0x028e});
    for(unsigned mask=0;mask<65536;++mask) {
        input::SwitchControllerState raw;
        for(unsigned bit=0;bit<16;++bit) raw.*input::kMappedButtons[bit]=(mask>>bit)&1;
        raw.left_x=1234;raw.right_y=-5678;
        const auto mapped=input::ApplyControllerMapping(map,raw);
        const auto xbox=input::ToGamepadState(mapped,1);
        const auto ps5=input::ToGamepadState(mapped,1,effective.vendor,effective.product);
        assert(ps5.vendor_id==0x054c && ps5.product_id==0x0ce6 && ps5.buttons==xbox.buttons &&
            ps5.left_trigger==xbox.left_trigger && ps5.right_trigger==xbox.right_trigger &&
            ps5.left_thumb_x==1234 && ps5.right_thumb_y==-5678);
    }
    auto copy=map;copy.gyro_assist=input::UseLocalGyroAssist(true,copy.gyro_assist);
    input::MotionSample sample;sample.valid=true;sample.source=0;sample.sequence=1;sample.sampled_ms=100;
    sample.gyro={0,1,1};sample.gravity={0,0,1};sample.orientation=input::MotionQuaternion{1,0,0,0};
    input::SwitchControllerState neutral;neutral.zl=true;
    input::GyroStickAssist assist;
    const auto untouched=assist.apply(copy,neutral,sample,100,true);
    assert(!assist.active() && untouched.right_x==0 && untouched.right_y==0);
    assert(input::LoadControllerMapping(home,"hk4e").value==map && map.gyro_assist);
    assert(!input::EncodeImuChannel(sample).empty());
    input::MotionSendPolicy gate;
    assert(!gate.admit(sample,100,16)); // PS identity never fabricates cloud activation.
    gate.enable(true);assert(gate.admit(sample,100,16));
    input::AndroidPs5MotionProbe probe;
    assert(!probe.update(100000,false,false,0));
    assert(!probe.update(110000,true,false,0));
    assert(!probe.update(129999,true,false,0));
    assert(std::string(probe.update(130000,true,false,0))=="MOTION_ANDROID_PS5_NO_ACTIVATION_20S");
    assert(!probe.update(140000,true,false,0));
    assert(std::string(probe.update(140001,true,true,1))=="MOTION_ANDROID_PS5_FIRST_LOCAL_SEND_NOT_ACK");
    assert(!probe.update(140002,true,true,2));
    input::AndroidPs5MotionProbe disconnected;
    assert(!disconnected.update(0,true,false,0));
    assert(!disconnected.update(19000,false,false,0));
    assert(!disconnected.update(21000,true,false,0));
    assert(!disconnected.update(40000,true,false,0));
    assert(disconnected.update(41000,true,false,0));
    input::AndroidPs5MotionProbe activated;
    assert(!activated.update(0,true,true,0));assert(!activated.update(30000,true,false,0));
    input::AndroidPs5MotionProbe backward;
    assert(!backward.update(50000,true,false,0));assert(!backward.update(10000,true,false,0));
    assert(!backward.update(29999,true,false,0));assert(backward.update(30000,true,false,0));
    input::AndroidPs5MotionProbe web_probe(GenshinInputMode::WebPs5);
    assert(!web_probe.update(0,true,false,0));
    assert(std::string_view(web_probe.update(20000,true,false,0))=="MOTION_WEB_PS5_NO_ACTIVATION_20S");
    assert(std::string_view(web_probe.update(20001,true,true,1))=="MOTION_WEB_PS5_FIRST_LOCAL_SEND_NOT_ACK");
    input::AndroidPs5MotionProbe current_probe(GenshinInputMode::CurrentPs5);
    assert(!current_probe.update(0,true,false,0));
    assert(std::string_view(current_probe.update(20000,true,false,0))=="MOTION_CURRENT_PS5_NO_ACTIVATION_20S");
    assert(std::string_view(current_probe.update(20001,true,true,1))=="MOTION_CURRENT_PS5_FIRST_LOCAL_SEND_NOT_ACK");
    assert(!current_probe.update(40000,true,true,2));
    std::filesystem::remove_all(directory); // Unique test-owned directory, no accounts.
    std::cout<<"PASS PS5 opt-in: original terminal/decoder preserved, bounded persistence/recovery, per-game scope, 65536 masks, native gate and evidence\n";
}
