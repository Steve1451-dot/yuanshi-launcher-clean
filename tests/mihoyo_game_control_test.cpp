#include "mihoyo/game_control.hpp"
#include <cassert>

int main() {
    using namespace cloudgenshin::control;
    const auto bytes = EncodeStartGame({"t","a","v","s","p",10});
    const std::vector<std::uint8_t> expected{0x45,0x67,0x4e,0x21,0,0,0,0,0,19,
        0x0a,1,'t',0x12,1,'a',0x1a,1,'v',0x20,10,0x32,1,'s',0x3a,1,'p',0x62,0,0x89,0xab};
    assert(bytes == expected);
    const auto packet = DecodePacket(bytes);
    assert(packet && packet->command == 20001 && packet->body.size() == 19);
    // StartGameReq timing fields, mirroring the official web SDK's
    // startGamingTimeMS/startConnectingTimeMS: 8 hasRtcConnected,
    // 9 sinceStartSec = ceil(elapsed/1000), 10 linkTasksMs, then the
    // empty H.264 game config (field 12).
    assert((EncodeStartGame({"t","a","v","s","p",10,true,2,1500})==std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x21,0,0,0,0,0,0x1a,
        0x0a,1,'t',0x12,1,'a',0x1a,1,'v',0x20,10,0x32,1,'s',0x3a,1,'p',
        0x40,0x01,0x48,0x02,0x50,0xdc,0x0b,0x62,0x00,0x89,0xab}));
    // common.SdkGameConfig (field 12) carries no gamepad field in the official
    // 7.1.0 client, so StartGameReq never declares controller capability; the
    // submessage stays an empty H.264 marker in every case.
    assert(DecodePacket(EncodeStartGame({"t","a","v","s","p",10}))->body.size() == 19);
    const Packet headed{20002,{8,7},{8,1,16,4,26,2,8,1}};
    const auto decoded = DecodePacket(EncodePacket(headed));
    assert(decoded && decoded->header == headed.header && decoded->body == headed.body);
    auto result = DecodeStartResult(headed.body);
    assert(result && result->accepted() && result->xbox_enabled);
    result = DecodeStartResult(std::vector<std::uint8_t>{8,0,16,4,26,0});
    assert(result && !result->accepted());
    assert(!DecodeStartResult(std::vector<std::uint8_t>{26,4,8,1}));
    assert(!DecodeStartResult(std::vector<std::uint8_t>{16,128,128,128,128,16}));
    assert(!DecodeStartResult(std::vector<std::uint8_t>{8,128}));
    assert(!EncodeStartGame({"","a","v","s","p",10}).size());
    auto bad = bytes; bad[5] = 1; assert(!DecodePacket(bad));
    bad = bytes; bad.back() = 0; assert(!DecodePacket(bad));
    bad = bytes; bad.push_back(0); assert(!DecodePacket(bad));
    assert(DecodePacket(EncodeStopGame("t"))->command == 20004);
    // Server-initiated StopGameRsp must be distinguishable from a transport
    // error; the view shows it as an ended session, not a retryable failure.
    assert(kStopGameRsp == 20005);
    // ResetInputsReq: official web client's resetInput() on a game control
    // channel error; same gameToken in field 1.
    const auto reset = EncodeResetInputs("t");
    const auto reset_packet = DecodePacket(reset);
    assert(reset_packet && reset_packet->command == 20030 && reset_packet->body.size() == 3 &&
           reset_packet->body[0] == 0x0a && reset_packet->body[1] == 1 && reset_packet->body[2] == 't');
    assert(EncodeResetInputs("").empty());
    for (std::size_t i=0;i<bytes.size();++i) assert(!DecodePacket(std::span(bytes).first(i)));

    // caGameConfig: PS pad only (field 2), Logitech GR0006 (field 9), and the
    // web control channel type (field 8).
    const auto ps_only = DecodeStartResult(std::vector<std::uint8_t>{8,1,16,4,26,4,8,0,16,1});
    assert(ps_only && ps_only->accepted() && !ps_only->xbox_enabled && ps_only->ps_enabled &&
           !ps_only->gr0006_enabled);
    const auto gr_only = DecodeStartResult(std::vector<std::uint8_t>{8,1,16,4,26,6,8,0,16,0,72,1});
    assert(gr_only && gr_only->gr0006_enabled && !gr_only->xbox_enabled && !gr_only->ps_enabled);
    const auto webctrl = DecodeStartResult(std::vector<std::uint8_t>{8,1,16,4,26,2,64,2});
    assert(webctrl && webctrl->control_channel_type == 2);

    // Official in-stream quality / fps requests (proto3 omits defaults).
    assert((EncodeSetFps(30) == std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x36,0x00,0x00,0x00,0x00,0x00,0x02,0x08,0x1e,0x89,0xab}));
    assert((EncodeSetGraphicsMode(0) == std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x44,0x00,0x00,0x00,0x00,0x00,0x00,0x89,0xab}));
    assert((EncodeSetGraphicsMode(2) == std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x44,0x00,0x00,0x00,0x00,0x00,0x02,0x08,0x02,0x89,0xab}));
    assert(EncodeSetFps(0).empty() && EncodeSetFps(241).empty());
    assert(EncodeSetGraphicsMode(3).empty());
    // proto.msg.SyncGameControlMode (20037) is server->client: the CA's
    // dispCmd decodes field 1 (int32) and forwards {"control_mode":...}.
    assert(DecodeSyncGameControlMode(std::vector<std::uint8_t>{0x08,0x03}).value_or(-1) == 3);
    assert(DecodeSyncGameControlMode(std::vector<std::uint8_t>{0x08,0x01}).value_or(-1) == 1);
    assert(DecodeSyncGameControlMode({}).value_or(-1) == 0);
    assert(DecodeSyncGameControlMode(std::vector<std::uint8_t>{0x12,0x01,0x00}).value_or(-1) == 0);
    assert(!DecodeSyncGameControlMode(std::vector<std::uint8_t>{0x08}));
    assert(!DecodeSyncGameControlMode(std::vector<std::uint8_t>{0x0d,0,0,0,0}));
    assert((EncodeKeyframeRequest()==std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x40,0,0,0,0,0,0,0x89,0xab}));
    const auto keyframe=DecodePacket(EncodeKeyframeRequest());
    assert(keyframe && keyframe->command==kKeyFrameReq && keyframe->body.empty());

    // proto.msg.Heartbeat: heartbeat_id in field 1, send_timestamp in field 2
    // (int64 each). Both the web SDK bundle and the native Android descriptor
    // ("heartbeat_id" 18 01 / "send_timestamp" 18 02) agree on these numbers.
    assert((EncodeHeartbeat(1, 1000) == std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x3d,0,0,0,0,0,5,0x08,0x01,0x10,0xe8,0x07,0x89,0xab}));
    assert(EncodeHeartbeat(0, 1000).empty() && EncodeHeartbeat(1, 0).empty());

    // proto.msg.SdkDeviceInfo (20017): the web SDK synchronises the client
    // platform/screen record to the pod right after the data channel connects.
    // Field order and the proto3 zero/default omissions follow the official
    // serializer (1,2,3,4,6,8,9,11,12,15,17,20).
    DeviceInfo info;
    info.game_token="t"; info.sdk_version="v"; info.terminal_type=10;
    info.device_id="d"; info.device_name="Nintendo Switch"; info.device_gpu="Tegra X1";
    info.device_system="Horizon OS";
    info.network_type="wifi"; info.server_ip="1.2.3.4"; info.transport_protocol="udp";
    info.screen_width=1280; info.screen_height=720;
    assert((EncodeSdkDeviceInfo(info)==std::vector<std::uint8_t>{
        0x45,0x67,0x4e,0x31,0,0,0,0,0,0x4e,
        0x0a,1,'t',0x12,1,'v',0x18,10,
        0x22,1,'d',
        0x32,15,'N','i','n','t','e','n','d','o',' ','S','w','i','t','c','h',
        0x42,8,'T','e','g','r','a',' ','X','1',
        0x4a,10,'H','o','r','i','z','o','n',' ','O','S',
        0x58,0x80,0x0a,0x60,0xd0,0x05,
        0x7a,4,'w','i','f','i',
        0x8a,0x01,7,'1','.','2','.','3','.','4',
        0xa2,1,3,'u','d','p',0x89,0xab}));
    DeviceInfo minimal;
    minimal.game_token="t"; minimal.sdk_version="v"; minimal.terminal_type=10;
    const auto minimal_device=DecodePacket(EncodeSdkDeviceInfo(minimal));
    assert(minimal_device && minimal_device->command==kSdkDeviceInfo &&
           minimal_device->body.size()==8);
    DeviceInfo broken=info; broken.game_token="";
    assert(EncodeSdkDeviceInfo(broken).empty());
    broken=info; broken.terminal_type=0;
    assert(EncodeSdkDeviceInfo(broken).empty());
    broken=info; broken.network_type="bad\n";
    assert(EncodeSdkDeviceInfo(broken).empty());
    broken=info; broken.device_gpu="bad\n";
    assert(EncodeSdkDeviceInfo(broken).empty());
    broken=info; broken.screen_width=70000;
    assert(EncodeSdkDeviceInfo(broken).empty());
    assert(kSdkDeviceInfo==20017);
}
