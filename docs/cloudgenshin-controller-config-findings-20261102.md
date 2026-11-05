# 云原神：能否直接下发、保存游戏内手柄键位

日期：2026-11-02。限定于用户提供的云原神 Android 7.1.0 APK，及工作区
已保存的官方网页 SDK。只读静态检查；没有连接账号、发送探测消息、修改云端
设置或读取凭据。生成的厂商源码仍留在工作区 reference 中，没有移植进产品。

## 结论

已确认云客户端可以上传手柄设备身份、按键状态、扳机和摇杆数值。
目前**未找到可确认的“上传整套游戏动作绑定表并永久保存”接口**。
不能把实时输入包、通用 SDK 消息容器或窗口确认/取消互换称作这个接口。
这不是“不可能”的证明：反编译有失败方法，通用消息有不透明载荷，也可能存在
未包含在该版本客户端中的私有/动态接口。

原神游戏内改键和云客户端本地映射是两个不同层次。游戏内通过设置界面改键的
能力，有[手柄使用 FAQ](https://www.taptap.cn/moment/389735054353369400)的说明；
该说明不证明云串流跨实例保存，更不代表存在可直接调用的配置导入协议。

## Java 层证据（reference/android/yscloud-7.1.0-3/jadx/sources）

- `com/combosdk/support/basewebview/joypad/GamepadButtonMapping.java`：
  `ButtonFunction` 只有 CONFIRM、CANCEL、L1_LB、R1_RB；映射用于窗口按键判断、
  图标和确认/取消互换。没有攻击、跳跃等游戏动作，也没有保存/网络上传调用。
- 同目录 `JoyPadConfig.java`：`setJoyPadExchange` 修改本进程静态字段并通知
  WebView/UI 订阅者。`set_joypad` 是 SDK 通知，不是改键上传成功回包。
- `com/miHoYo/sdk/WebViewModule.java:436` 的 `setJoYPadExchange` 只是调用上面的
  本地 setter；不能拿这个名字当作云端游戏改键方法。
- `com/p043mihoyo/cgsdk/GamepadManager.java` 的 `sendGamepadState` 从
  `GamepadInstance.getXinputData()` 提取状态，上传 VID/PID、buttons、triggers、
  left/right thumb 数值。这里没有动作名或绑定表。
- `ClientCore.java` 公开 JNI 接口包括设备列表、实时状态、reset input、通用
  game data、码率、帧率、串流档位等；未识别到单独的键位配置读写方法。
- `ComboConst.ModuleCallFuncName.Notice.SET_GAME_CONFIG` 也是误导性的相似名字：
  `NoticeHandler.setGameConfig(game, gameBiz)` 只设置公告模块的游戏标识。
- 另一个 `MDKConfig.setGameConfig` 是登录 SDK 初始化配置；`GameConfig` 的内容
  是应用/渠道、设备、区域等字段，不是玩家的按键配置。
- `BaseSdkHolder.handleOnGameData` 是游戏到客户端的 SDK/WebView 调用转发。
  可看到登录、语言、支付、保活等处理，未识别到反向导入游戏按键表的处理链。

## 原生协议证据（arm64-v8a/libnetio.so 内嵌 descriptor）

已解析七组协议：`msg/cmd_client_to_ca.proto`、`common/datachannel.proto`、
`common/controller.proto`、`msg/cmd_game_controller.proto`、`common/model.proto`、
`common/game_data.proto`、`msg/cmd_paas_to_ca.proto`。

- `RtcGamepadKey` 的字段 1–10 是 VID、PID、buttons、左右扳机、左右摇杆 XY、
  RTT 时间戳；`RtcGamepadList` 是设备 VID/PID 列表，feedback 是震动反馈。
- `ControllerStateSync.gamepad_state` 仍只有实时状态。
- `CaGameConfig` 里的 Xbox/PS/GR0006 是支持开关；`SdkGameConfig` 是串流能力配置。
- `SyncGameControlMode.mode` 是模式同步，不是动作绑定表。
- `ResetInputsReq` 只有 game token，用来释放按住的输入；不是重置/写入游戏键位。
- `RtcGameData`、`SdkGameDataMessage`、`DataChannelMessage` 可以携带通用字节载荷。
  存在容器不等于找到了方法名、参数格式、成功回包和跨会话持久化机制。

另外检索了全部 Java 源码、六个 arm64 原生库中的相关字符串，以及保存的
`cg-sdk.c4be3733.js` 和 `web.b959658b.js`。网页的 Xbox/Sony button mapping
是浏览器按钮索引到标准手柄状态的转换，也不是游戏动作绑定配置。

## 可复查工具和后续边界

`scripts/inspect-apk-descriptor-fields.py` 是无第三方依赖的只读 metadata 工具。
示例（仓库目录）：

```text
python scripts/inspect-apk-descriptor-fields.py ../../reference/android/yscloud-7.1.0-3/unpacked/lib/arm64-v8a/libnetio.so common/datachannel.proto --message Gamepad
```

没有根据猜测实现一个“云端保存”按钮，也没有修改用户的云端键位。
要认定直接下发可用，仍需找到官方调用方、确切方法/字段、方向和成功确认，
并在退出后重新分配的会话验证键位保留。通过普通手柄操作游戏设置页，是另一种
方案，不是已经发现直接配置 API；本次未实现或执行该自动操作。

本次 0.2.102 仅修原神本地体感水平反向，不增加任何键位预设或云端改键功能。
