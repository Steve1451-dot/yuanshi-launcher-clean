# 云·原神 7.1.0 协议表（从 APK 描述符静态提取）

来源：`yscloud_7.1.0.apk` 的 `lib/arm64-v8a/libnetio.so`。该 `.so` 未加壳，
`.rodata` 里直接嵌入了 20 个 `FileDescriptorProto`（protobuf 描述符），
本次用一个最小 protobuf 解析器把它们全部解出。**下面的字段号/类型是编译器
产物，不是猜测。**

提取方法（可复现）：在文件里定位 `0x0a <len> "xxx.proto"` 作为
`FileDescriptorProto` 起点，按 1=name, 2=package, 4=message_type, 5=enum_type,
12=syntax 解析。

## 1. 命令号分组

`libnetio.so` 里所有 `*_CmdId_IsValid` 的立即数（反汇编 `movz w8,#imm`）分成三组：

| 区间 | 含义 | 例子 |
| --- | --- | --- |
| 20001–20041 (0x4e21–0x4e49) | 客户端↔CA 的网络命令 | StartGameReq=20001, RtcDataChannel=20010, SyncGameControlMode=20037 |
| 63001–63203 (0xf619–0xf6e3) | **本机守护进程管道**（cmd_game_controller.proto） | DaemonInitReq=63001, MihoyoSDKPipeSync=63101, ControllerDeviceSync=63201 |
| 0x27xx | KCP / 网络探测 | KcpPong, StunRsp, SdpInfo |

> 结论：`ControllerDeviceSync/StateSync/FeedbackSync`（63201/63202/63203）属于
> **本机 IPC**，和 `DaemonInitReq`、`MihoyoSDKPipeSync` 同一编号空间，
> 不是发给 pod 的。手柄能力**不能**通过它们上报给服务器。

## 2. 客户端→CA 命令表（0x4e00 家族，实测 39 条）

```
20001 StartGameReq        20002 StartGameRsp       20003 RtcSignalNotify
20004 StopGameReq         20005 StopGameRsp        20006 RtcPauseReq
20007 RtcPauseRsp         20008 RtcResumeReq       20009 RtcResumeRsp
20010 RtcDataChannel      20011 RtcReAuthReq       20012 RtcReAuthRsp
20013 RtcGameData         20014 RtcNotPlayingTips  20015 RtcKeepPlaying
20016 SdkStatsInfo        20017 SdkDeviceInfo      20018 RtcGameStasReport
20019 RtcSetBitrate       20020 SdkWifiInfo        20021 RtcKeepaliveCfg
20022 RtcSetFps           20023 RtcSetQosPolicy     20024 RtcPingTestReq
20025 RtcPingTestRsp      20026 StopPingTestReq    20027 StopPingTestRsp
20028 ClientActionTimestamp 20029 Heartbeat        20030 ResetInputsReq
20032 KeyFrameReq         20033 SdkFeatureSwitchInfo 20035 RtcVqcState
20036 RtcSetGraphicsMode  20037 SyncGameControlMode 20038 ResetWindowSizeReq
20039 RtcSetBitrateMultiplier 20040 RtcHapticAeraList 20041 GenshinUgcFpsUpdate
```

## 3. StartGameReq / StartGameRsp

```
msg.StartGameReq
   1 game_token string      2 aid string          3 sdk_version string
   4 terminal_type uint32   5 offline_mock_info   6 sid string
   7 pod_id string          8 has_rtc_connected   9 since_start_sec uint32
  10 link_tasks_ms uint32  11 mock_game_mode     12 game_config -> common.SdkGameConfig

msg.StartGameRsp
   1 retcode int32          2 game_app_id uint32  3 game_config -> common.CaGameConfig
```

```
common.SdkGameConfig          common.CaGameConfig
  1 enable_wrapped_h265 bool    1 gamepad_enable_xbox int32
  2 is_big_isp bool             2 gamepad_enable_ps int32
                                3 enable_p2p_tcp bool
                                4 enable_p2p_dynamic_switch bool
                                5 h265_blacklist
                                6 super_resolution_blacklist
                                7 enable_super_resolution bool
                                8 web_control_channel_type int32
                                9 gamepad_enable_gr0006 int32
                               10 enable_sei bool ... 16 arrival_jitter_estimator_field_trials
```

**`SdkGameConfig` 只有两个 bool，没有任何手柄字段。** 0.2.56 猜测的
`08 01 10 de 08 18 8e 05` 是无依据的编码，已撤销。客户端无法在开局时声明手柄。

`CaGameConfig.gamepad_enable_xbox/ps/gr0006` 是服务器单方面下发的能力位，
实测恒为 0。它不是客户端能填的请求字段。

## 4. RtcDataChannel（命令 20010，所有输入帧）

```
 1 type               enum common.DataType
 2 keyboard           common.RtcKeyboard     {1 key_code, 2 rtt_trace_send_time, 3 keyboard_layout, 4 capslock, 5 numlock}
 3 mouse              common.RtcMouse        {1 cursor_x double, 2 cursor_y double, 3 z_delta, 4 rel_x, 5 rel_y, 6 rtt, 7 button_type}
 4 mouse_feedback
 5 ime_input          6 ime_input_feedback  7 clipboard  8 clipboard_feedback
 9 video_state        10 gamepad             common.RtcGamepadKey
11 gamepad_list        common.RtcGamepadList {1 vendor_id repeated uint32, 2 product_id repeated uint32}
12 gamepad_feedback    13 touch_list         common.RtcTouchEventList
14 imu_event           15 imu_feedback
17 input_control_type  uint32   ← bitmask of ClientCore.GameContorlType (type 31 only)
18 message_trace_id    uint64
```

`common.DataType`（输入帧用到的值）：

```
 1 KEYBOARD_DOWN   2 KEYBOARD_UP   3 MOUSE_MOVE
 4 MOUSE_LBUTTON_DOWN  5 MOUSE_LBUTTON_UP  6 MOUSE_RBUTTON_DOWN  7 MOUSE_RBUTTON_UP
 8 MOUSE_MBUTTON_DOWN  9 MOUSE_MBUTTON_UP 10 MOUSE_ZDELTA
16 VIDEO_STATE    17 GAMEPAD_KEY   18 GAMEPAD_LIST   19 GAMEPAD_FEEDBACK
21 TOUCH_EVENT_LIST   22 IMU_EVENT  23 IMU_FEEDBACK
30 GAME_DATA  31 RESET_GAME_INPUT_CONTROL
```

```
common.RtcGamepadKey
  1 vendor_id uint32  2 product_id uint32  3 buttons uint32
  4 left_trigger uint32 5 right_trigger uint32
  6 left_thumb_x int32 7 left_thumb_y int32 8 right_thumb_x int32 9 right_thumb_y int32
 10 rtt_trace_send_time int64

common.RtcTouchEvent
  1 finger_id uint32  2 action_type uint32
  3 point_x FLOAT     4 point_y FLOAT      ← wire type 5 (fixed32)
  5 press_size_x float 6 press_size_y float
  7 is_primary bool    8 rtt_trace_send_time int64

common.RtcTouchEventList
  1 event_list repeated common.RtcTouchEvent
```

**触摸坐标是 float。** 除描述符外，`libnetio.so` 的
`common::RtcTouchEvent::_InternalSerialize`（0x1a5ef4）在字段 3 处是：

```
fmov s0, w8          ; 单精度
mov  w8, #0x1d       ; tag = (3<<3)|5
stur s0, [x1, #1]    ; 4 字节
strb w8, [x1], #5
```

0.2.57 及以前写的是 double（wire type 1），服务器会把它当未知字段丢掉，
四舍五入等于所有触摸都点在 (0,0)。0.2.58 已改为 float。

## 5. 控制模式

```
enumerate.GameControlMode:  0 kDefault  1 kTouch  2 kKeyboard  3 kGamepad
msg.SyncGameControlMode:    1 mode int32        (CmdId 20037, 服务器→客户端)
```

`SyncGameControlMode` **不是客户端能发的命令**。7.1.0 的
`libclient_core_impl.so` 里，它在 `ClientCoreImpl::dispCmd` 被解码：CA 打
`sync game control mode: %d`，再通过 `onGameInfo -1039` 把
`{"control_mode":"gamepad"|"default"}` 交给 App 更新界面。客户端发包方向没有
这个命令。

客户端切输入源用 `RtcDataChannel` 的 type 31
`RESET_GAME_INPUT_CONTROL`：field 17 是位掩码
（`KEYBOARD=1, MOUSE=2, GAMEPAD=4, TOUCH_SCREEN=8`）。官方 App 只在一处调用
`resetGameInputControl(TOUCH_SCREEN)`，原生层发出 `{type=31, field17=8}`。
type 17/18/21 的常规帧不写 field 17。

```
enumerate.ClientTerminalType
  0 CTT_NONE  1 CTT_PC  2 CTT_IOS  3 CTT_ANDROID  4 CTT_MAC
  5 CTT_WEB_PC  6 CTT_WEB_IOS  7 CTT_WEB_ANDROID  8 CTT_WEB_MAC
  9 CTT_WEB_TOUCH  10 CTT_WEB_KEYBOARD  700 CTT_PRELOAD
```

本项目的默认终端是 10（WebKeyboard）。官方安卓客户端是 3（Android）。
`cloudgenshin-terminal.txt` 可在不改包的情况下切换。

## 6. 对客户端的落地改动（0.2.59）

1. `input_packet.cpp`：触摸 `point_x/point_y` 改 float（真 bug，保留）。
2. `input_packet.cpp`：常规帧（17/18/21）不再写 `input_control_type`；
   新增 type 31 `EncodeResetGameInputControlChannel(bitmask)`。
3. `game_control.cpp`：删除上游 `SyncGameControlMode` 编码器；新增
   `DecodeSyncGameControlMode`（field 1 int32）。
4. `rtc_session.cpp`：收到 20037 时打 `SERVER_GAME_CONTROL_MODE code=N`
   （3=pod 已切手柄），同一值只报一次。

## 7. 仍未解决 / 不可知

- pod 是否按 type-17 帧使用虚拟手柄：只能实测。`SERVER_GAME_CONTROL_MODE`
  是 pod 侧切换成功的直接回执。
- `gamepad_enable_xbox=0` 的成因：`StartGameRsp` 只下发一次，终端类型
  （10 默认，可切 3=Android）是客户端仅剩的可控变量。
- pod 端（CA）是独立二进制，不在 APK 内；虚拟手柄驱动是否存在无法静态验证。
