#pragma once

// Pure protocol helpers of the Endfield streaming session (no curl / no
// libpeer): sdk_msg parsing, answer-candidate rewriting and payload-type
// extraction. Kept separate so they can be host-tested against the exact
// semantics of the official web bundle.

#include <cstddef>
#include <optional>
#include <string>

namespace cloudgenshin::endfield {

// sdk_msg fields needed by the browser WebRTC path (findings 6/14.2).
struct VlinkSdkData {
    std::string gs_ip;     // gsIp (single line, or the first multi-line entry)
    int web_rtc_port = 0;  // webRtcPort (or gsPort)
    std::string gsm_token;
    std::string user_id;
    std::string player_id;
    std::string player_role;
    // Raw JSON dumps of the identity fields. The official getSDPInfo copies
    // userId/gsmToken/playerId verbatim, so their JSON types are preserved.
    std::string gsm_token_json = "\"\"";
    std::string user_id_json = "\"\"";
    std::string player_id_json = "\"\"";
    // JS truthiness of sdk_msg.playerRole gates playerInfo in the official
    // page; Number(playerRole) is then used for SDPInfo.user.r.
    bool player_info_valid = false;
    long long player_role_number = 0;
    int codec_type = 18;  // 18 = H264
    // Normalized first-connect payload. The official page passes sdk_msg's
    // packedData straight into btoa(encodeURI(...)); it is normally a string,
    // but some dispatch versions return a JSON object. Objects are serialized
    // here so the wire payload is never silently empty.
    std::string packed_data;
    std::string packed_data_kind;  // "missing"/"string"/"object"/"array"/"scalar"/"null"
    std::size_t packed_data_length = 0;
    // Presence bits of the official ClientFirstOnline JSON keys when
    // packedData is an object (diagnostics only; values are never logged).
    unsigned packed_data_key_mask = 0;
    std::string auth;
    std::string verify;
    std::string assign_result_param;
    std::string game_id;
    std::string instance_id;
    std::string node_id;
    // Pod scheme/version. The official client only uses the ARM branches when
    // schemeType == "arm_ks" (module 2038 E = {ARM:"arm_ks", X86:"x86"}).
    std::string scheme_type;
    std::string gs_ver;

    bool ready() const { return !gs_ip.empty() && web_rtc_port > 0; }

    bool is_arm_game_server() const { return scheme_type == "arm_ks"; }
    // IS_PROXY_ARM: ARM pod with gsVer major < 5 (player module @1975708).
    // Only this branch appends "/webrtc-proxy" to gsUrl and rewrites every
    // candidate to the fixed srflx relay line (KH, @1922454).
    bool is_proxy_arm() const {
        if (!is_arm_game_server()) return false;
        int major = 0;
        for (const char c : gs_ver) {
            if (c >= '0' && c <= '9') {
                major = major * 10 + (c - '0');
            } else {
                break;
            }
        }
        return major < 5;
    }
};

// Lenient parser for the dispatch/poll sdk_msg JSON (gsIp string or array).
std::optional<VlinkSdkData> ParseVlinkSdkMsg(const std::string& sdk_msg_json);
// swap_gs_info.function.input_ver selects the actual controller wire format.
std::optional<int> ParseServerInputVersion(const std::string& json);

// H264/opus payload types of the pod's answer; true only when both are found.
bool ParseAnswerPayloadTypes(const std::string& sdp, int* video_payload, int* audio_payload);

// BH(sdp, ip, "a=candidate", port) of the official bundle (@1921xxx):
// every IPv4 literal on candidate lines becomes `ip`, then the port segment
// between the ip and the following " typ host" becomes `port`.
std::string RewriteCandidateLines(const std::string& sdp, const std::string& ip, int port);

// KH(sdp, ip, port) of the official bundle (@1922454), used only when the pod
// is an ARM proxy (is_proxy_arm()). Every candidate section is replaced by one
// fixed srflx line pointing at the relay endpoint.
std::string RewriteProxyArmCandidates(const std::string& sdp, const std::string& ip, int port);

}  // namespace cloudgenshin::endfield
