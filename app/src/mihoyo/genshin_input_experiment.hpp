#pragma once
#include "atomic_file_replace.hpp"
#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>

namespace cloudgenshin::input {
// Append modes: old persisted values and numeric diagnostic codes stay stable.
// CurrentPs5 changes only the pad, preserving the already-working RTC terminal.
enum class GenshinInputMode {Standard,AndroidPs5,WebPs5,CurrentPs5};
inline std::string GenshinInputModePath(const std::string& home) {
    return home+"/genshin-input-mode.txt";
}
// A separate, bounded preference: never overwrite diagnostic terminal/pad
// overrides, login data or per-game button mappings when switching back.
inline GenshinInputMode LoadGenshinInputMode(const std::string& home) {
    const auto path=GenshinInputModePath(home);
    for(const auto& file:{path,path+".bak"}) {
        std::ifstream in(file,std::ios::binary);
        if(!in) continue;
        in.seekg(0,std::ios::end);const auto size=in.tellg();
        if(size<0 || size>64) continue;
        in.seekg(0);
        std::string magic,mode;unsigned version=0;
        if(!(in>>magic>>version>>mode) || magic!="CGNX-INPUT" || version!=1) continue;
        in>>std::ws;
        if(!in.eof()) continue;
        if(mode=="standard") return GenshinInputMode::Standard;
        if(mode=="android-ps5") return GenshinInputMode::AndroidPs5;
        if(mode=="web-ps5") return GenshinInputMode::WebPs5;
        if(mode=="current-ps5") return GenshinInputMode::CurrentPs5;
    }
    return GenshinInputMode::Standard;
}
inline bool SaveGenshinInputMode(const std::string& home,GenshinInputMode mode) {
    if(mode!=GenshinInputMode::Standard && mode!=GenshinInputMode::AndroidPs5 &&
       mode!=GenshinInputMode::WebPs5 && mode!=GenshinInputMode::CurrentPs5) return false;
    const auto path=GenshinInputModePath(home);
    std::ofstream out(path+".tmp",std::ios::binary|std::ios::trunc);
    if(!out) return false;
    out<<"CGNX-INPUT 1\n"<<(mode==GenshinInputMode::AndroidPs5?"android-ps5":
        mode==GenshinInputMode::WebPs5?"web-ps5":
        mode==GenshinInputMode::CurrentPs5?"current-ps5":"standard")<<'\n';
    out.flush();const bool good=out.good();out.close();
    return good && !out.fail() && opennow::storage::ReplaceWithTemporaryFile(path+".tmp",path);
}
inline bool AndroidPs5Experiment(std::string_view profile,GenshinInputMode mode) {
    return profile=="hk4e" && mode==GenshinInputMode::AndroidPs5;
}
inline GenshinInputMode ScopedGenshinInputMode(std::string_view profile,GenshinInputMode mode) {
    return profile=="hk4e" && (mode==GenshinInputMode::AndroidPs5 || mode==GenshinInputMode::WebPs5 ||
        mode==GenshinInputMode::CurrentPs5)
        ? mode : GenshinInputMode::Standard;
}
inline bool Ps5InputExperiment(GenshinInputMode mode) {
    return mode==GenshinInputMode::AndroidPs5 || mode==GenshinInputMode::WebPs5 ||
        mode==GenshinInputMode::CurrentPs5;
}
inline const char* GenshinInputModeEvent(GenshinInputMode mode) {
    switch(mode) {
        case GenshinInputMode::AndroidPs5:return "GENSHIN_ANDROID_PS5_EXPERIMENT_ACTIVE";
        case GenshinInputMode::WebPs5:return "GENSHIN_WEB_PS5_EXPERIMENT_ACTIVE";
        case GenshinInputMode::CurrentPs5:return "GENSHIN_CURRENT_PS5_EXPERIMENT_ACTIVE";
        default:return "GENSHIN_STANDARD_INPUT_ACTIVE";
    }
}
struct SessionInputIdentity {
    unsigned terminal;
    std::uint32_t vendor,product;
    bool operator==(const SessionInputIdentity&) const = default;
};
inline SessionInputIdentity ResolveSessionInputIdentity(GenshinInputMode mode,SessionInputIdentity standard) {
    // APK CTT_ANDROID=3, Sony DualSense VID/PID. The state payload is still
    // the APK's XinputData; phone IMU remains a separate type-22 message.
    if(mode==GenshinInputMode::AndroidPs5) return {3,0x054c,0x0ce6};
    // Deliberately fix the proven web terminal, rather than inheriting an old
    // terminal override: this is a one-variable comparison against web/Xbox.
    if(mode==GenshinInputMode::WebPs5) return {10,0x054c,0x0ce6};
    // No HTTP/RTC identity migration, decoder preference or mapping mutation.
    if(mode==GenshinInputMode::CurrentPs5) return {standard.terminal,0x054c,0x0ce6};
    return standard;
}
inline bool UseLocalGyroAssist(bool experiment,bool preference) {
    return !experiment && preference; // Never mix native IMU and rate-to-stick.
}
// Worker-only one-shot evidence, sampled with the existing 5-second counters.
// Missing activation is not proof the service does not support motion. A local
// send is not an ACK, game consumption or aim-direction acceptance.
class AndroidPs5MotionProbe {
public:
    explicit AndroidPs5MotionProbe(GenshinInputMode mode=GenshinInputMode::AndroidPs5):mode_(mode) {}
    const char* update(std::int64_t now,bool input_ready,bool requested,std::uint64_t sent) {
        ever_requested_|=requested;
        if(sent && !sent_reported_) {
            sent_reported_=true;
            return mode_==GenshinInputMode::CurrentPs5?"MOTION_CURRENT_PS5_FIRST_LOCAL_SEND_NOT_ACK":
                mode_==GenshinInputMode::WebPs5?"MOTION_WEB_PS5_FIRST_LOCAL_SEND_NOT_ACK":
                "MOTION_ANDROID_PS5_FIRST_LOCAL_SEND_NOT_ACK";
        }
        if(!input_ready) {ready_at_.reset();return nullptr;}
        if(!ready_at_ || now<*ready_at_) ready_at_=now;
        if(!ever_requested_ && !timeout_reported_ && now-*ready_at_>=20000) {
            timeout_reported_=true;
            return mode_==GenshinInputMode::CurrentPs5?"MOTION_CURRENT_PS5_NO_ACTIVATION_20S":
                mode_==GenshinInputMode::WebPs5?"MOTION_WEB_PS5_NO_ACTIVATION_20S":
                "MOTION_ANDROID_PS5_NO_ACTIVATION_20S";
        }
        return nullptr;
    }
private:
    std::optional<std::int64_t> ready_at_;
    GenshinInputMode mode_;
    bool ever_requested_=false,timeout_reported_=false,sent_reported_=false;
};
} // namespace cloudgenshin::input
