# 云·原神 7.1.0 APK 逆向：手柄开关取证的结论（2026-10-22）

分析对象：用户提供的官方客户端 `yscloud_7.1.0.apk`（85 MB）。
只用静态解析，不改客户端、不绕过任何校验。libnetio / libclient_core 均未 strip
的部分给出了完整的 protobuf 符号与字段全名表，足以回答"客户端能不能告诉服务器
我有手柄"。

## 结论：`StartGameReq` 里没有手柄字段

`msg.StartGameReq`（command 20001）在这个版本里的字段全名表只有：

```
msg.StartGameReq.game_token
msg.StartGameReq.aid
msg.StartGameReq.sid
msg.StartGameReq.pod_id
msg.StartGameReq.sdk_version
```

外加两个子消息（由 `_Internal` 访问器确认）：`game_config`（`common.SdkGameConfig`）
和 `offline_mock_info`（`msg.MockDispatchInfo`）。

`common.SdkGameConfig` 没有任何 `_Internal::<field>` 访问器，字段全名表里也没有
`common.SdkGameConfig.*` 条目，说明它是一个没有子消息字段的配置占位（proto3 下
标量字段不会生成 `_Internal` 访问器，字段名也不进 full-name 表）。

因此"在 StartGameReq 里声明 Xbox 手柄"这个方向**没有可依据的字段**。0.2.56 曾按
猜测在 field 12 写入手柄 vendor/product，实测服务器仍返回 `GAME_CONFIG_XBOX=0`，
与这里的静态结论一致：字段不存在，服务器不会读。

## 结论：`CaGameConfig` 同样没有手柄开关字段

`common.CaGameConfig` 的已知字段只有 `h265_blacklist`、`super_resolution_blacklist`
（`_Internal` 访问器）与 `arrival_jitter_estimator_field_trials`（full-name 表）。
本机实测 `GAME_CONFIG_XBOX/PS/GR0006 = 0` 读的是别处的位图（`game_config` 里的
枚举/标志位），而不是这三个命名布尔字段的命名映射；具体位布局不在符号里。

## 真正存在的手柄通道

`msg/cmd_game_controller.proto` 是一个独立的控制器协议文件，含：

| 消息 | 字段（来自 `_Internal` 访问器） |
| --- | --- |
| `msg.ControllerDeviceSync` | `description`（唯一字段；字符串） |
| `msg.ControllerStateSync` | `description`, `gamepad_state`, `imu_state`, `keyboard_state`, `mouse_state`, `touch_state`, `video_state`, `data_channel_message` |
| `msg.ControllerFeedbackSync` | `description`, `camera_info`, `data_channel_message`, `genshin_scene_info`, `genshin_ui_event`, `nap_ui_event` |

三者都带 `CmdId`（`ControllerDeviceSync_CmdId_IsValid/descriptor`），即它们是**独立命令**，
不是 `StartGameReq` 的成员。`ControllerDeviceSync` 只有 `description` 一个字符串字段，
看起来最像"上报本机接了哪些手柄"。

**但反汇编 `CmdId_IsValid` 后结论反转**（`movz w8,#imm` 立即数）：

```
ControllerDeviceSync   = 63201 (0xf6e1)
ControllerStateSync    = 63202 (0xf6e2)
ControllerFeedbackSync = 63203 (0xf6e3)
```

它们和 `DaemonInitReq=63001`、`MihoyoSDKPipeSync=63101` 同属 **0xf6xx 本机
守护进程管道编号空间**，而网络命令全部在 0x4e21–0x4e49（20001–20041）。
所以这三条是 App 与本地 daemon 之间的 IPC，**不会发给 pod**，无法用来向
服务器声明手柄。

完整的 20 个 proto 描述符已解出，见
`docs/cloudgenshin-protocol-schema-7.1.0.md`。

## 同版本里另有两条与手柄有关的字符串

在 `libclient_core_impl.so`：

- `app disabled gamepad.` —— 官方客户端里存在"应用禁用了手柄"这条状态，说明手柄
  开关确由服务端/配置决定，客户端只能读。
- `gamepad is bluetooth: %d` —— 客户端会把"手柄是否蓝牙连接"作为一项上报。
- `onGamepadDevices(vector<pair<u16,u16>>)` —— 报告的正是 (vendor, product) 列表，
  与我们发的 `RtcGamepadList` 字段口一致。

## 对本项目的结论

- `StartGameReq` 里没有任何手柄字段，客户端侧"开局声明手柄"到此为止。
- `SyncGameControlMode`（20037）是**服务器→客户端**：`dispCmd` 解码后经
  `onGameInfo -1039` 交给 App。客户端不能靠它请求手柄。
- 客户端唯一能发的输入控制帧是 `RtcDataChannel` type 31
  `RESET_GAME_INPUT_CONTROL`，field 17 是
  `ClientCore.GameContorlType` 位掩码（1/2/4/8）。常规 17/18/21 帧不写它。
  0.2.59 已按官方实现。
- 触摸链路另有一个确定的编码错误：`RtcTouchEvent.point_x/point_y` 是 float，
  0.2.57 写成 double，服务器会把坐标当未知字段丢弃。0.2.58 已修，0.2.59 保留。
- 不要再在 `StartGameReq` 里猜字段：描述符里的字段集合就是完整集合。
