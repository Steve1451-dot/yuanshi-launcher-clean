#pragma once
#include "mihoyo/gamer_login.hpp"
#include "atomic_file_replace.hpp"

#include <fstream>
#include <string>
#include <string_view>

namespace cloudgenshin {
// Public application configuration of the official CN cloud clients.
// Every value below comes from miHoYo's own published artifacts (production
// web bundles for the H5 clients; package assets and manifest for the
// Android clients). They are application/signing constants, never account
// credentials: no token, cookie or account data belongs in this table.
struct GameProfile {
    std::string_view id;               // persisted selection key (cloudgenshin-game.txt)
    std::string_view game_biz;         // official game identifier used on every endpoint
    std::string_view display_name;     // label shown in the Chinese UI
    std::string_view latin_name;       // label shown in the English UI
    std::string_view cloud_base;       // gamer login + dispatcher API root
    std::string_view sdk_base;         // combo account/QR/combo API root
    std::string_view combo_app_key;    // official Combo SDK application key
    std::string_view dispatch_app_key; // official cloud dispatcher signing key
    std::string_view op_biz;           // x-rpc-op_biz value of the official client
    // Cloud SDK version (CG_SDK_VERSION of the official web bundle). It is the
    // sdkVersion of StartGameReq/SdkDeviceInfo, not the account SDK version.
    std::string_view sdk_version;
    std::string_view app_version;      // x-rpc-app_version device metadata
    unsigned app_id;                   // official Combo app_id (4/8)
    std::string_view client_type;      // x-rpc-client_type of the official client variant
    std::string_view cps;              // x-rpc-cps value of the official client
    std::string_view cloud_app_id;     // x-rpc-app_id on cloud-game calls ("" = Combo app id)
};

inline constexpr GameProfile kGameProfiles[] = {
    {"hk4e", "hk4e_cn", "云·原神", "Genshin Cloud",
     "https://api-cloudgame.mihoyo.com/hk4e_cg_cn",
     "https://hk4e-sdk.mihoyo.com/hk4e_cn",
     "d0d3a7342df2026a70f650b907800111",
     "7511caf05a67bb7ca33b1970ce6222a8",
     "clgm_cn",
     "6.1.0.64", "7.0.0", 4, "19", "", ""},
    {"hkrpg", "hkrpg_cn", "云·星穹铁道", "Star Rail Cloud",
     "https://cg-hkrpg-api.mihoyo.com/hkrpg_cn/cg",
     "https://hkrpg-sdk.mihoyo.com/hkrpg_cn",
     "4650f3a396d34d576c3d65df26415394",
     "1fbf60a3582bf2ed05810954ee2349b9",
     "clgm_hkrpg-cn",
     "6.2.0.25", "4.6.0", 8, "19", "", ""},
};

// CTT_WEB_KEYBOARD = 10. The mobile/H5 keyboard family is the terminal that
// carries a virtual pad: the pod derives CaGameConfig gamepadEnableXbox/Ps from
// the terminal, and the Windows PC marker (5) came back with both flags off.
// The marker file still overrides this for diagnostics.
inline constexpr unsigned kWebKeyboardTerminal = 10;

// Diagnostic override for the CA terminal type (cloudgenshin-terminal.txt).
inline unsigned LoadTerminalTypeOverride(const std::string& path, unsigned fallback) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return fallback;
    unsigned value = 0;
    if (!(in >> value)) return fallback;
    if (value == 0 || value > 700) return fallback;
    return value;
}

inline const GameProfile& DefaultGameProfile() { return kGameProfiles[0]; }

inline const GameProfile* FindGameProfile(std::string_view id) {
    for (const auto& profile : kGameProfiles)
        if (profile.id == id) return &profile;
    return nullptr;
}

inline const GameProfile* FindGameProfileByBiz(std::string_view game_biz) {
    for (const auto& profile : kGameProfiles)
        if (profile.game_biz == game_biz) return &profile;
    return nullptr;
}

inline std::string AppIdText(const GameProfile& profile) {
    return std::to_string(profile.app_id);
}

// Selection persistence mirrors the language preference file: a small text
// file in the app directory plus the automatic .bak copy.
inline const GameProfile& LoadGameSelection(const std::string& path) {
    for (const auto& file : {path, path + ".bak"}) {
        std::ifstream in(file, std::ios::binary);
        if (!in) continue;
        std::string value(16, '\0');
        in.read(value.data(), static_cast<std::streamsize>(value.size()));
        value.resize(static_cast<std::size_t>(in.gcount()));
        while (!value.empty() && (value.back() == '\n' || value.back() == '\r'))
            value.pop_back();
        if (const auto* profile = FindGameProfile(value)) return *profile;
    }
    return DefaultGameProfile();
}

inline bool SaveGameSelection(const std::string& path, const GameProfile& profile) {
    const std::string temporary = path + ".tmp";
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << std::string(profile.id);
    out.flush();
    const bool good = out.good();
    out.close();
    if (!good || out.fail()) return false;
    return opennow::storage::ReplaceWithTemporaryFile(temporary, path);
}

// Mirrors the request identity of the official client for the selected game:
// the H5 cloud clients for hk4e/hkrpg, so the device metadata follow the profile.
inline auth::WebDeviceIdentity NativeWebIdentity(const GameProfile& profile,
                                                 const std::string& device_id) {
    return {"CloudGenshin-NX/0.2 (Nintendo Switch; native WebRTC)",
            std::string(profile.client_type), std::string(profile.app_version), "Horizon", device_id,
            "Nintendo Switch", "Switch", std::string(profile.op_biz), "zh-cn",
            std::string(profile.cps)};
}
} // namespace cloudgenshin
