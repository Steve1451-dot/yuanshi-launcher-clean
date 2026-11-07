#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace cloudgenshin::control {
inline constexpr std::uint16_t kStartGameReq = 20001;
inline constexpr std::uint16_t kStartGameRsp = 20002;
inline constexpr std::uint16_t kStopGameReq = 20004;
// proto.msg.StopGameRsp: server-initiated end of the cloud game session. It
// arrives on the signaling proxy and on some pods on the RTC data channel.
// It is not a generic signaling failure; the UI must distinguish it.
inline constexpr std::uint16_t kStopGameRsp = 20005;
// proto.msg.ResetInputsReq: the official client sends it through the signaling
// proxy when the independent game control channel disconnects, so the pod
// clears any key state the client held.
inline constexpr std::uint16_t kResetInputsReq = 20030;
// proto.msg.SdkDeviceInfo: the official client synchronises its platform and
// screen information to the pod as soon as the RTC data channel is connected.
inline constexpr std::uint16_t kSdkDeviceInfo = 20017;
inline constexpr std::uint16_t kSetFpsReq = 20022;
inline constexpr std::uint16_t kSetBitrateReq = 20019;
inline constexpr std::uint16_t kSetGraphicsModeReq = 20036;
// proto.msg.SyncGameControlMode (20037) is server->client. The CA's
// ClientCoreImpl::dispCmd decodes it, logs "sync game control mode: %d" and
// forwards {"control_mode":"gamepad"|"default"} to the app (onGameInfo -1039).
// The client cannot send it upstream; the client->pod mode switch is the
// RtcDataChannel type-31 reset frame in input_packet.hpp.
inline constexpr std::uint16_t kSyncGameControlMode = 20037;
inline constexpr std::uint16_t kKeyFrameReq = 20032;

// proto.enumerate.GraphicsMode: KHIGHQUALITY = 0, KTRAFFICSAVING = 1,
// KEXTREMESAVING = 2. These are the official floating-ball quality tiers.
inline constexpr std::int32_t kGraphicsHighQuality = 0;
inline constexpr std::int32_t kGraphicsTrafficSaving = 1;
inline constexpr std::int32_t kGraphicsExtremeSaving = 2;

struct Packet {
    std::uint16_t command = 0;
    std::vector<std::uint8_t> header, body;
};
std::vector<std::uint8_t> EncodePacket(const Packet& packet);
std::optional<Packet> DecodePacket(std::span<const std::uint8_t> bytes);

struct StartGame {
    std::string game_token, account_id, sdk_version, sid, pod_id;
    std::uint32_t terminal_type = 0;
    // Official web SDK timing fields on StartGameReq. startGamingTimeMS and
    // startConnectingTimeMS are set to the same instant before connecting:
    // 8 has_rtc_connected (0 at send time, omitted by proto3),
    // 9 since_start_sec = ceil(elapsed_ms / 1000),
    // 10 link_tasks_ms = elapsed_ms.
    bool has_rtc_connected = false;
    std::uint32_t since_start_sec = 0;
    std::uint32_t link_tasks_ms = 0;
};
struct StartResult {
    std::int32_t retcode = 0;
    std::uint32_t game_app_id = 0;
    bool has_config = false;
    // proto.common.CaGameConfig field 1: gamepadEnableXbox.
    bool xbox_enabled = false;
    // proto.common.CaGameConfig field 2: gamepadEnablePs.
    bool ps_enabled = false;
    // proto.common.CaGameConfig field 9: gamepadEnableGr0006.
    bool gr0006_enabled = false;
    // proto.common.CaGameConfig field 8: webControlChannelType.
    std::int32_t control_channel_type = 0;
    // RtcSucc is 1 in the official SDK, not the HTTP API's success code 0.
    bool accepted() const { return retcode == 1 && game_app_id != 0 && has_config; }
};
std::vector<std::uint8_t> EncodeStartGame(const StartGame& request);
std::vector<std::uint8_t> EncodeStopGame(const std::string& game_token);
// proto.msg.ResetInputsReq carries the same game token in field 1.
std::vector<std::uint8_t> EncodeResetInputs(const std::string& game_token);
std::vector<std::uint8_t> EncodeHeartbeat(std::uint64_t id, std::int64_t unix_ms);
// proto.msg.RtcSetFps / RtcSetGraphicsMode sent over the signaling proxy
// channel; the official client also omits proto3 default values.
// proto.msg.SdkDeviceInfo (20017). Only the fields the web SDK actually fills
// are carried; unset ones stay omitted like any proto3 message. The web SDK's
// syncDeviceInfoToCa fills 1/2/3/4/6/8/9/11/12/15/17/20: deviceId, the WebGL
// renderer string and the dispatch server address are part of the record.
struct DeviceInfo {
    std::string game_token, sdk_version, device_id, device_name, device_gpu;
    std::string device_system, network_type, server_ip, transport_protocol;
    std::uint32_t terminal_type = 0, screen_width = 0, screen_height = 0;
};
std::vector<std::uint8_t> EncodeSdkDeviceInfo(const DeviceInfo& info);
std::vector<std::uint8_t> EncodeSetFps(std::uint32_t fps);
// Official RtcSetBitrate: uint32 min_bitrate (1), max_bitrate (2), in bits/s.
std::vector<std::uint8_t> EncodeSetBitrate(std::uint32_t minimum,std::uint32_t maximum);
// Official web SDK keyframe request over the signaling proxy; empty protobuf.
std::vector<std::uint8_t> EncodeKeyframeRequest();
std::vector<std::uint8_t> EncodeSetGraphicsMode(std::int32_t mode);
// Decodes proto.msg.SyncGameControlMode field 1 (int32). Returns std::nullopt
// for a malformed body; an absent field decodes as 0 (GameControlMode default).
std::optional<std::int32_t> DecodeSyncGameControlMode(std::span<const std::uint8_t> body);
std::optional<StartResult> DecodeStartResult(std::span<const std::uint8_t> body);
// RtcDataChannel type 23 / field 15 RtcImuFeedback / field 1 bool.
// Only a well-formed feedback message may activate sensor transmission.
std::optional<bool> DecodeImuFeedback(std::span<const std::uint8_t> body);
} // namespace cloudgenshin::control
