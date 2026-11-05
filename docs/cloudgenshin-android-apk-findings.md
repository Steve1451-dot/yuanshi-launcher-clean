# Cloud Genshin Android 7.1.0: static protocol findings

Inspected on 2026-10-11. This is an interoperability reference, **not evidence
of a working Switch login or game stream**. The original APK is unchanged.
No account session, token, cookie or QR contents were captured or retained.

## Artifact and tools

- Input: user-provided `yscloud_7.1.0_3.apk`, 85,237,554 bytes.
- SHA-256: `AFE345A1561D3A9D3AE6D67B82D95BEDF8E48CCD0C54DA3B5333D55D2F86E209`.
- Manifest package: `com.miHoYo.cloudgames.ys`; version name `7.1.0`;
  version code `400000047`. The filename suffix is not the manifest version code.
- Six DEX files and native libraries for arm64-v8a / armeabi-v7a.
- JADX 1.5.6 decoded the manifest/resources and emitted sources, finishing with
  118 decompilation errors. Findings below use readable fields/call sites and
  native disassembly; they do not assume every decompiled method is correct.
- Local reference outputs are under
  `reference/android/yscloud-7.1.0-3/{jadx,unpacked}` in the parent workspace.
  These generated vendor sources are not copied into the native app.

## Verified dispatch-to-native mapping

`DispatchFinishResult.java` has the serialized-name annotations:

| Primary JSON name | Accepted alias | Role |
| --- | --- | --- |
| `session_id` | `sid` | Session identifier |
| `sdk_msg` | `sdk_param` | Base64-encoded SDK startup parameters |

`BaseSdkHolder.java:1577` reads `finish_result.getSdk_msg()` after `FINISHED`,
then passes it to `startGame` at line 1606.
`MihoyoSdkHolder.java:2123` passes the actual game data into
`ClientCore.startGame(activity, surfaceView, gameData, 20000)`.
`ClientCore.java:688` passes that string to `nativeStartGame`.
The `fake` / `nativeStartGame2` branches are developer test paths, not the
normal dispatcher result flow.

The arm64 `libclient_core_impl.so` function
`ClientCoreImpl::startGame(void*, bool, char const*, unsigned int)` makes:

1. `common::string_util::base64_decode` at `0x68d48`;
2. `paas::SdkStartGameParams` construction at `0x68d80`;
3. `google::protobuf::MessageLite::ParseFromArray` at `0x68dac`.

This establishes the format without reading a live session value. The
previously saved public web SDK also declares `proto.paas.SdkStartGameParams`
with repeated fields `[10,12,15,19,21,23]`, and the web launcher passes
`sdk_param` to `startGame`. Android and web share this message shape; this does
not prove their whole connection/authentication lifecycle is identical.

## Recovered startup schema

`libnetio.so` preserves the serialized `paas/pb.proto` FileDescriptorProto.
The local `reference/android/inspect_proto.py` prints field metadata only.

| Fields | Names / types |
| --- | --- |
| 1–5 | `sid`, `ca_id`, `pod_id`, `game_token`, `resolution`: string |
| 6–9 | `target_fps`: int32; `cmd_line`, `game_svr_addr`: string; `rtc_udp_port`: uint32 |
| 10–12 | `additional_game_svr_addrs`: repeated string; `game_server_wss_url`: string; `additional_game_server_wss_urls`: repeated string |
| 13–15 | `big_isp`: uint32; `game_control_channel_url`: string; `additional_game_control_channel_urls`: repeated string |
| 16–19 | `is_ipv6`: bool; `game_svr_addr_ipv6`: string; `rtc_udp_port_ipv6`: uint32; `additional_game_svr_addrs_ipv6`: repeated string |
| 20–23 | `game_server_wss_url_ipv6`, `game_control_channel_url_ipv6`: string; their alternatives at 21 and 23: repeated string |

`game_token` is ephemeral authorization, not a public configuration constant.
WSS addresses and control-channel addresses are separate fields; do not blindly
substitute one for the other or log any value containing authorization.

## An additional authentication/control step exists

The embedded `msg/cmd_client_to_ca.proto` descriptor defines `msg.StartGameReq`:

| Field | Name / type |
| --- | --- |
| 1–4 | `game_token`, `aid`, `sdk_version`: string; `terminal_type`: uint32 |
| 5 | `offline_mock_info`: `msg.MockDispatchInfo` (not the live path) |
| 6–7 | `sid`, `pod_id`: string |
| 8–11 | `has_rtc_connected`: int32; `since_start_sec`, `link_tasks_ms`, `mock_game_mode`: uint32 |
| 12 | `game_config`: `common.SdkGameConfig` |

`msg.StartGameRsp` carries `retcode` (int32), `game_app_id` (uint32), and
`game_config` (`common.CaGameConfig`). The native function `sendStartGameReq`
constructs this message, then calls `sendTcpMsg` with command ID **20001**
(`0x6d940`–`0x6d944`). `sendTcpMsg` serializes a `netio::Packet` and sends it
through `netio::SignalingDataChannel::sendSignalingData` (`0x6f008`, `0x6f078`).
The native client also has `onAuthPassed(msg::StartGameRsp&)`.

These are static call/schema observations. The exact outer packet envelope,
RPC correlation, response handling, terminal identity and connection order
still need verification before implementing a live sender. A bare WSS/RTC
connection must not be labelled authenticated or playable.

## Native implementation made from these findings

- Added `mihoyo/sdk_start_game_params.{hpp,cpp}`: independent bounded Base64 +
  protobuf-wire decoder for all 23 startup fields. No Android/JNI/protobuf
  runtime is linked. Proto3 absent fields retain defaults; callers still must
  validate required authentication fields, endpoints and TLS before use.
- Limits: 64 KiB decoded message, 8 KiB per known string, 16 alternatives per
  address list, 1024 wire fields. Rejects malformed lengths/varints, bad UTF-8,
  NUL/control characters, incorrect known wire types and conflicting sessions.
  Skips bounded unknown varint/fixed64/bytes/fixed32 fields; rejects groups.
- Updated both initial and ticket dispatch parsing to accept the observed
  aliases and retain decoded startup parameters in memory. Conflicting aliases
  or malformed supplied parameters yield `InvalidResponse` without exposing
  partial session material. Responses without startup data remain metadata-only.
- Test fixture was serialized independently by Python `google.protobuf` using
  the APK descriptor, with synthetic authorization and `.invalid` endpoints.
  It exercises every field, repeated values and IPv6. No live requests are
  performed by these tests.

## Verification and remaining work

The two narrow host tests (`mihoyo_sdk_start_game_params`,
`mihoyo_dispatch_response`) pass under C++20 with warnings as errors. Coverage
includes the independent fixture, unpadded Base64, protobuf defaults/duplicates,
unknown fields, overflow/truncation, text/size limits, alias conflicts and
session mismatch; the wire decoder also runs 4096 deterministic malformed
inputs. The devkitPro Switch cross-build passes and emits `SwitchNOW.nro`.

**At this earlier decoder-only milestone, that NRO was still the GFN application,
not a usable Cloud Genshin release.** The status below is historical and is
superseded by the 2026-10-11 integration note.
The mihoyo adapters are not yet instantiated by its launcher; unused code may
be removed by linker garbage collection. Compilation does not establish live
login, queue, authenticated control, video, sound or controller delivery.

Next: verify the control packet envelope and official user-driven login/node
selection flow, implement their lifecycle, then connect a real dispatch result
to authenticated signaling and the existing media/input stack. Only a real
Switch session entering a game picture establishes completion. Android native
libraries reference `liblog.so`, `libOpenSLES.so`, Android libc and related
services, so matching ARM64 alone does not make them runnable in an NRO.

## Native integration update (2026-10-11)

The launcher now invokes the real Cloud Genshin QR/HTTP/dispatch/RTC/media
modules. See `cloudgenshin-integration-status.md` for current implementation,
verification and limitations; this is still an integration alpha, not a
real-Switch gameplay success claim.

Additional static evidence used in this build:

- `QRManager.login` uses `ElementId.Combo.name` (`Combo`) and serializes
  `UserData.toDataJSON()` into `payload.raw`. That JSON includes Combo app,
  channel, open ID and authorization, with optional channel-specific data.
- `SdkLoginManager.java:142` converts the successful Combo `entity.getOpenId()`
  to a long and checks `getAidWhiteList()`; lines 162–169 pass the same open ID
  into login storage. The official CN profile therefore supports numeric
  open ID as aid when no explicit passport aid/uid is supplied. Native fallback
  is restricted to app_id=4/channel_id=1 and positive signed-64-bit decimal
  IDs; this is not a cross-channel or other-game mapping claim.
- The control envelope is magic 45 67, command uint16 big-endian, header size
  uint16 big-endian, body size uint32 big-endian, header then body, tail 89 ab.
  StartGameRsp success is 1, distinct from HTTP retcode 0. Authentication must
  succeed before peer creation/client hello. The websocket keepalive is empty
  text, not an invented ping JSON command.

No Android native library is copied into or executed by the NRO. All adapted
protocol implementations are independent C++ and use the existing Switch
H.264/Opus/Deko3D/libpeer infrastructure. Static schema evidence and synthetic
tests do not establish live service acceptance or controller delivery.

## Login and verification follow-up (2026-10-12)

- PlatformHandler.java:331–337 and 613–621 send Combo login with app_id,
  channel_id, device, stringified account data and Tools.signNew.
  MDKCNModule.java:744–746 supplies uid/token/guest=false. Tools.java:151–165
  sorts field names, joins key=value pairs with ampersands and HMAC-SHA256 signs
  that string. Native Account exchange uses that flow without copying Android
  SDK code or accepting a nonzero retcode as authorization.
- ComboNetClient.java:284 includes x-rpc-device_id in common request headers.
  Native QR requests previously sent the same device in both bodies, but
  omitted this header; revision 0.2.1 adds the consistent header.
- CommonResponse.java:26 / 158–161 identifies -3101 as RET_NEED_AIGIS.
  LoginManager.java:1754–1791 and 2511–2540 obtains x-rpc-aigis from response
  headers, calls JiyanUtils.loginGeeTest, and retries password/SMS requests
  only with its completed result. JiyanUtils.java uses GT3/GTCaptcha4 Android
  clients. Their Android Activity/dialog implementation is not a Switch UI.
- Revision 0.2.1 detects only presence of that verification header. It does
  not render, solve, or submit a slider, and does not implement SMS login.
  The real Switch user's -3503 report did not include such a header or raw
  message, so it cannot yet be claimed to be an available interactive challenge.

## Native RTT/node follow-up (2026-10-12)

The embedded SDK public WebSocketRttTester sends twenty text X frames and uses
FIFO replies to calculate rounded mean RTT. ClientCore.doSpeedTest constructs
ping_server_id/node_id/display_name/region_id/province_id/isp_type metadata and
rtt/loss_rate/jitter/out_of_order. The web dispatcher serializes bandwidth
(Mbps) plus ping_results as node. Official SDK supports skipping bandwidth;
zero is not a throughput measurement. Native revision 0.2.2 implements this
WSS echo path, with bounded concurrency, metadata and deadlines.

ListPingSvrRsp contains repeated PingSvrInfo in protobuf field 1. PingSvrInfo
fields 1..9 are uint32 ID, addr, display_name, region_id, int64 province_id,
uint32 isp_type, node_id, wss_url and bool is_ipv6. Strings remain in RAM.
The current official web entry web.b959658b.js was read without login on
2026-10-12; it still selects web speed_client_type numeric 7. Android's
GetNodesInfoReq uses a string, so Android typing alone is not evidence to
change the web-shaped native request. Real Switch -110013 remains unverified
after this change; lack of a full speed-test path is corrected, not declared
to be the conclusively reproduced backend cause.
