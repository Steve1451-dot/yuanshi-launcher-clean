#pragma once

// 云·终末地 (Endfield Cloud) official wire constants.
//
// Transport values come from reference/web/endfield/index.cda000.js; game/API
// bootstrap values also follow the user's Android 1.6.0 APK. Provenance lives
// in docs/cloudgenshin-endfield-findings.md and endfield-apk-port-2026-10-25.md.
// No guessed IDs, keys or opaque tokens are defined here.
//
// Scope: rtcType=WEBRTC browser path (ws-proxy client-offer + "welink"
// DataChannel + CtrlMsg input), which is what we replicate with libpeer.

#include <cstdint>
#include <string_view>

namespace cloudgenshin::endfield {

// --- Cloud game backend (prod) -------------------------------------------
inline constexpr std::string_view kCloudServerUrl = "https://cloud-game.hypergryph.com";
// X-APP-CODE header, used on every cloud-game API request.
inline constexpr std::string_view kServerAppCode = "be6ybz1j0UpYIS8V";
// OAuth appCode for the Hypergryph passport login (used when we add QR login).
inline constexpr std::string_view kOauthAppCode = "dd7b852d5f1dd9da";
// WeLink signaling service; ws-proxy endpoint = kSignalDomain + "/ws-proxy".
inline constexpr std::string_view kSignalDomain = "wss://webrtc-signaling.vlinkcloud.cn";
inline constexpr std::string_view kSignalProxyPath = "/ws-proxy";
// WeLink cert-hash host used by the WL_RTC path (not used by us; recorded only).
inline constexpr std::string_view kHashGwHost = "transfer-inner.wl-times.com";

// --- Client identity ------------------------------------------------------
// X-Client-App-Version (the web app version).
inline constexpr std::string_view kClientAppVersion = "1.6.0";
// Companion WebRTC gameManager's API profile; APK HeaderInterceptor instead
// uses android. Account verification has its independently verified identity.
inline constexpr std::string_view kClientPlatform = "web";
// WeLink web SDK version: SDPInfo.local.v, biz_data.clientVer.
inline constexpr std::string_view kWeLinkSdkVersion = "5.14.1.260916160035-wlweb-release";
// getSDPInfo().local.t / osCategory in biz_data.
inline constexpr std::string_view kClientChannel = "h5";
// getSDPInfo().local.os / biz_data.os. Official enum values:
// ANDROID/IPAD/IOS/MAC/WINDOWS/LINUX, fallback "h5".
// Switch port note: the client keyboard table sends Windows VK codes on all
// platforms, so we identify as WINDOWS for pod-side input/cursor semantics.
// Single point of change if a live test says otherwise (findings 14.8).
inline constexpr std::string_view kOsName = "WINDOWS";

// --- Dispatch / streaming defaults (traffic saving) -----------------------
// AndroidSdk bridge profile from APK CloudBridge (not the current H5 launch).
inline constexpr int kGameBridgePlatform = 1;
inline constexpr bool kGameBridgeIsWeb = false;
// Current libpeer transport uses the companion H5 gameManager profile. Its
// dispatch platform (2) and game bridge / U8 platform (7) are distinct enums.
inline constexpr int kDispatchCloudPlatform = 2;
inline constexpr bool kDispatchIsWeb = true;
inline constexpr int kCloudTicketPlatform = 7;
// codec_type: H264 (default value of the web client's codec getter before
// WLRTC negotiation; our offer only advertises H264).
inline constexpr int kCodecTypeH264 = 18;
// Upper limits (kbps) of BIT_RATE_LEVEL_MAP; dispatch sends upperLimit.
inline constexpr int kBitRateLow = 4000;
inline constexpr int kBitRateMedium = 8000;
inline constexpr int kBitRateHigh = 12000;
// Our default: lowest tier (省流).
inline constexpr int kDefaultBitRate = kBitRateLow;
inline constexpr std::string_view kDefaultBitRateLevel = "low";
// User-requested low-bitrate/60 FPS default, in dispatch and runtime settings.
inline constexpr int kDispatchFps = 60;
// Our runtime 省流 preference (applied via SetFps after the first frame).
inline constexpr int kDefaultStreamFps = 60;
// Dispatch video resolution (16:9; libreathing decoder path).
inline constexpr int kDispatchWidth = 1280;
inline constexpr int kDispatchHeight = 720;
// pay_type: 1 = normal queue (we never switch to the paid quick queue).
inline constexpr int kPayTypeNormal = 1;
// Dispatch response queuePeriod on staging; poll cadence.
inline constexpr int kQueuePollSeconds = 10;

// --- SDPInfo (authV2) -----------------------------------------------------
inline constexpr int kSampleRate = 48000;
inline constexpr int kAudioChannels = 2;
inline constexpr std::string_view kAudioFormat = "opus";
// function block of the authV2 payload.
inline constexpr int kInputVersion = 3;         // highest advertised (input_ver:[1,2,3])
inline constexpr bool kSupportCmdV2 = true;     // support_cmd_v2
// SetDPI is sent only to ARM pods (schemeType "arm_ks"); official fallback 480.
inline constexpr int kDefaultDpi = 480;
// intercept_app arg, sent once after the first decoded video frame.
inline constexpr std::string_view kInterceptAppArg =
    "{\"transFile\":true,\"transJson\":true,\"transTxt\":false}";

// --- DataChannel protocol -------------------------------------------------
inline constexpr std::string_view kChannelName = "welink";
inline constexpr int kHeartbeatWelinkMs = 2000;  // canned WelinkHeartBeat cadence
inline constexpr int kHeartbeatGsProxyMs = 1000; // SDKHeartBeat cadence after game start
inline constexpr int kReadyIntervalMs = 1000;    // literal "READY" cadence after answer
inline constexpr std::string_view kReadyMessage = "READY";
// SimpleCmd command names (module 6718, tag -> field mapping in findings 14.3).
inline constexpr std::string_view kCmdChannelType = "ChannelType";
inline constexpr std::string_view kCmdLocalIm = "LocalIm";
inline constexpr std::string_view kCmdAuthV2 = "authV2";
inline constexpr std::string_view kCmdAuth = "auth";
inline constexpr std::string_view kCmdPackedData = "PackedData";
inline constexpr std::string_view kCmdCtrlMsg = "CtrlMsg";
inline constexpr std::string_view kCmdClientOnline = "ClientOnline";
inline constexpr std::string_view kCmdSdkHeartBeat = "SDKHeartBeat";
inline constexpr std::string_view kCmdSetDpi = "SetDPI";
inline constexpr std::string_view kCmdChangeAvc = "changeAVC";
inline constexpr std::string_view kCmdExitGame = "exitGame";
inline constexpr std::string_view kCmdStartProgress = "启动进度";
inline constexpr std::string_view kCmdKickOffline = "踢下线";
inline constexpr std::string_view kCmdProtoVersion = "protoVersion";

// Canned SimpleCmd payloads (exact bytes; see findings 14.4 -- do not retype).
inline constexpr std::uint8_t kChannelTypeCanned[] = {
    0x0A, 0x0B, 'C', 'h', 'a', 'n', 'n', 'e', 'l', 'T', 'y', 'p', 'e', 0x12, 0x09,
    0xE4, 0xB8, 0xBB, 0xE9, 0x80, 0x9A, 0xE9, 0x81, 0x93, 0x1A, 0x09,
    '3', '0', '1', '5', '0', '0', '1', '0', '0', 0x22, 0x01, '0', 0x2A, 0x02, '2', '3',
};
inline constexpr std::uint8_t kWelinkHeartBeatCanned[] = {
    0x0A, 0x09, 0xE6, 0xB5, 0x8B, 0xE5, 0xBB, 0xB6, 0xE6, 0x97, 0xB6, 0x12, 0x0D,
    0x31, 0x36, 0x37, 0x38, 0x30, 0x38, 0x34, 0x32, 0x33, 0x37, 0x36, 0x36, 0x37,
    0x22, 0x01, 0x34,
};
inline constexpr std::uint8_t kExitGameCanned[] = {
    0x0A, 0x12, 0xE7, 0x94, 0xA8, 0xE6, 0x88, 0xB7, 0xE4, 0xB8, 0xBB, 0xE5, 0x8A, 0xA8,
    0xE9, 0x80, 0x80, 0xE5, 0x87, 0xBA,
};
// Keep-alive frame (one OneInputOPData: cursor {1,1}, device 999, value 1).
inline constexpr int kKeepAliveDevice = 999;

// Device / op enums (CG.PROTO.OneInputOPData; findings 9.1/9.2).
inline constexpr int kDevKeyboard = 1;
inline constexpr int kDevMouse = 2;
inline constexpr int kDevXInput = 3;
inline constexpr int kDevGamepad = 4;
inline constexpr int kDevTouch = 16;
inline constexpr int kOpDefault = 0;
inline constexpr int kOpMouseWheel = 515;
inline constexpr int kOpMouseMov = 516;
inline constexpr int kOpXInputButtons = 1024;
inline constexpr int kOpXInputLTrigger = 1025;
inline constexpr int kOpXInputRTrigger = 1026;
inline constexpr int kOpXInputLx = 1027;
inline constexpr int kOpXInputLy = 1028;
inline constexpr int kOpXInputRx = 1029;
inline constexpr int kOpXInputRy = 1030;
inline constexpr int kStateDefault = 1;
inline constexpr int kStateDown = 2;
inline constexpr int kStateUp = 3;

}  // namespace cloudgenshin::endfield
