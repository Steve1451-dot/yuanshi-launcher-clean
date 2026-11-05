#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace cloudgenshin::dispatch {

// Android 7.1.0: paas.SdkStartGameParams, carried in base64 sdk_msg/sdk_param.
// This contains ephemeral game authorization. Keep it in memory; never log,
// serialize to disk, or include it in an exception/diagnostic message.
struct SdkStartGameParams {
    std::string sid;
    std::string ca_id;
    std::string pod_id;
    std::string game_token;
    std::string resolution;
    std::int32_t target_fps = 0;
    std::string cmd_line;
    std::string game_svr_addr;
    std::uint32_t rtc_udp_port = 0;
    std::vector<std::string> additional_game_svr_addrs;
    std::string game_server_wss_url;
    std::vector<std::string> additional_game_server_wss_urls;
    std::uint32_t big_isp = 0;
    std::string game_control_channel_url;
    std::vector<std::string> additional_game_control_channel_urls;
    bool is_ipv6 = false;
    std::string game_svr_addr_ipv6;
    std::uint32_t rtc_udp_port_ipv6 = 0;
    std::vector<std::string> additional_game_svr_addrs_ipv6;
    std::string game_server_wss_url_ipv6;
    std::vector<std::string> additional_game_server_wss_urls_ipv6;
    std::string game_control_channel_url_ipv6;
    std::vector<std::string> additional_game_control_channel_urls_ipv6;
};

// Bounded wire decoding only. Proto3 fields can be absent. A successful parse
// does not imply authorization, endpoint safety, or a playable session; the
// connection owner must validate required fields and TLS before using them.
std::optional<SdkStartGameParams> ParseSdkStartGameParams(
    std::span<const std::uint8_t> protobuf);
std::optional<SdkStartGameParams> DecodeSdkStartGameParams(
    std::string_view base64);
// Bounded canonical Base64 for SDK protobufs; never log decoded material.
std::optional<std::vector<std::uint8_t>> DecodeSdkBase64(std::string_view base64);

} // namespace cloudgenshin::dispatch
