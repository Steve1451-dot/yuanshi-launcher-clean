#pragma once
#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>

namespace cloudgenshin {
// Explicit opt-in, no credentials or executable commands in this marker.
inline bool AutoTestEnabled(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    char bytes[32]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_AUTOTEST_V1\n";
}
// Single-variable title-entry probe, scoped to Star Rail and the normal
// explicit test opt-in. No identity, gamepad wire or in-game mapping change.
inline bool AutoTestStarRailPointerEnabled(const std::string& home,std::string_view app_id) {
    if(app_id!="8" || !AutoTestEnabled(home+"/cloudgenshin-autotest.txt")) return false;
    std::ifstream file(home+"/cloudgenshin-autotest.star-rail-pointer",std::ios::binary);
    char bytes[48]{};file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_HKRPG_POINTER_V1\n";
}
// Native re-grant did not resolve Star Rail's game-side 1001 error. Retain it
// only as an independent bounded diagnostic, never extra HTTP on normal entry.
inline bool AutoTestStarRailFreshSdkLoginEnabled(const std::string& home,std::string_view app_id) {
    if(app_id!="8" || !AutoTestEnabled(home+"/cloudgenshin-autotest.txt")) return false;
    std::ifstream file(home+"/cloudgenshin-autotest.star-rail-sdk-refresh",std::ios::binary);
    char bytes[48]{};file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_HKRPG_SDK_REFRESH_V1\n";
}
inline bool AutoTestStarRailWebResumeEnabled(const std::string& home,std::string_view app_id) {
    if(app_id!="8" || !AutoTestEnabled(home+"/cloudgenshin-autotest.txt")) return false;
    std::ifstream file(home+"/cloudgenshin-autotest.star-rail-web-resume",std::ios::binary);
    char bytes[48]{};file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_HKRPG_WEB_RESUME_V1\n";
}
inline bool AutoTestStarRailWebGameTokenEnabled(const std::string& home,std::string_view app_id) {
    if(!AutoTestStarRailWebResumeEnabled(home,app_id)) return false;
    std::ifstream file(home+"/cloudgenshin-autotest.star-rail-game-token",std::ios::binary);
    char bytes[48]{};file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_HKRPG_GAME_TOKEN_V1\n";
}
// Separate opt-in for this diagnostic session, never a user-default change.
// The ordinary autotest marker is also required. No fee, priority queue,
// repeated dispatch or arbitrary duration can be expressed in this file.
inline bool AutoTestLongNormalQueueEnabled(const std::string& home) {
    if(!AutoTestEnabled(home+"/cloudgenshin-autotest.txt")) return false;
    std::ifstream file(home+"/cloudgenshin-autotest.longqueue",std::ios::binary);
    char bytes[48]{};file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_LONG_QUEUE_30M_V1\n";
}
// A separate bounded soak opt-in; the ordinary marker still means 170 seconds.
// This marker alone cannot launch a session, change quality or enable inputs.
inline int AutoTestStreamSeconds(const std::string& path) {
    std::ifstream file(path,std::ios::binary);char bytes[32]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_SOAK_600_V1\n"
        ? 600 : 170;
}
inline bool AutoTestUiProbeEnabled(const std::string& path) {
    std::ifstream file(path,std::ios::binary);char bytes[32]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_UI_PROBE_V1\n";
}
// Six local settings open/close cycles, after the normal entry window. Never
// selects quality, sends game input, exports files or confirms any dialog.
inline int AutoTestUiProbePhase(std::int64_t elapsed_ms) {
    if(elapsed_ms<100000) return 0;
    const auto phase=(elapsed_ms-100000)/3000+1;
    return phase<12 ? static_cast<int>(phase) : 12;
}
// Explicit opt-in for exactly one entry click: an unattended run may send the
// same user-requested Continue action once after the in-game SDK login, so
// entering the game can be verified without a human. Without this marker the
// client never clicks, exactly like before.
inline bool AutoTestEntryClickEnabled(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    char bytes[32]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_ENTRY_CLICK_V1\n";
}
// Independent, explicit Endfield allocation diagnostic. Does not select a
// paid queue; the caller also requires the ordinary autotest marker.
inline bool AutoTestEndfieldAlternateNodeEnabled(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    char bytes[48]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_ENDFIELD_ALT_NODE_V1\n";
}
// Arm only after inspecting the Endfield title frame. Independent from the
// mouse-click permission; never enables a keyboard chord or repeated buttons.
inline bool AutoTestEndfieldConfirmEnabled(const std::string& path) {
    std::ifstream file(path,std::ios::binary);
    char bytes[48]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_ENDFIELD_CONFIRM_V1\n";
}
enum class AutoTestAction { None, LaunchOnce, CancelTimeout, Finished };
// Separate login-only probe. Does not automatically consume cloud-game time,
// delete saved accounts, export the QR, or click through phone verification.
inline bool AutoTestPassportLoginEnabled(const std::string& path) {
    std::ifstream file(path,std::ios::binary);char bytes[64]{};
    file.read(bytes,sizeof(bytes));
    return file.eof() && std::string(bytes,static_cast<std::size_t>(file.gcount()))=="CGNX_PASSPORT_LOGIN_V1\n";
}
// UI-thread owner. One normal queue attempt per process; never retries or pays.
class AutoTestPolicy {
public:
    AutoTestPolicy(bool enabled,std::int64_t now_ms):enabled_(enabled),deadline_(now_ms+180000) {}
    bool enabled() const {return enabled_;}
    void stop() {done_=true;}
    AutoTestAction poll(std::int64_t now_ms,bool signed_in,bool busy,bool cancelled) {
        if(!enabled_ || done_) return AutoTestAction::None;
        if(cancelled) {
            if(busy) return AutoTestAction::None; // wait for queue/session cleanup
            done_=true;return AutoTestAction::Finished;
        }
        if(launched_ && !busy) {done_=true;return AutoTestAction::Finished;}
        if(!timed_out_ && now_ms>=deadline_) {timed_out_=true;return AutoTestAction::CancelTimeout;}
        if(timed_out_) return AutoTestAction::None;
        if(!launched_ && signed_in && !busy) {
            launched_=true;
            // Unattended normal-queue attempts may sit behind a long queue;
            // only give up after half an hour so a 10-30 minute wait can
            // still reach a pod without switching or paying for a queue.
            deadline_=now_ms+1800000;
            return AutoTestAction::LaunchOnce;
        }
        return AutoTestAction::None;
    }
private:
    bool enabled_,launched_=false,done_=false,timed_out_=false;
    std::int64_t deadline_;
};
} // namespace cloudgenshin
