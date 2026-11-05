# 终末地 APK 协议对照（2026-10-25）

用户提供 `C:/Users/y/Downloads/EndfieldCloud-1-1.6.0.apk` 后，本轮改为直接反编译该 APK 和内置 `assets/wl_game.apk`，移植安卓客户端的游戏桥约定。不是把 Android ELF 库放进 Switch 运行，也不把传输成功当作游戏加载成功。

## 来源与可复核范围

- 主 APK SHA-256：`ABBD3EE37CEBA9FDC627E5AA5DB14850CF4205E495438C692E07F1ECD2CDFB45`。
- `reference/android/endfield-cloud-1.1.6.0-jadx/sources`：10836 个类，JADX 报告 126 个反编译错误，不能声称整个 APK 无误还原。本轮使用的 CloudBridge、LoginService、ModuleManagement.getOauth2Code、NewApi.grant、CloudQueueHelper、OneChannelLoginHelper、HeaderInterceptor、GamepadManager、WebViewService 的关键方法已直接查看，未标为未反编译。
- `reference/android/endfield-wl-game-jadx/sources`：218 个类，反编译完成。内置 arm64 `libwlcgcore.so` 已提取供只读符号/指令对照；`SendDataToGameWithKey`、`InputV2`、`GamePadStatusEventV3` 等是 JNI 入口。晚间重新反编译后确认 `pcgxdq/pcgfzi.java:1305` 的 6042 连接就绪分支调用 `pcgwqv.pcgcrk.pcgfzi`，后者在 `pcgwqv/pcgcrk.java:143` 调用 `SendStartGameKey`；因此早先“没有调用证据”的描述已被纠正。JNI 入口为 0x177264，接收启动 bytes / 长度 / SDPInfo 并派发给 native 连接对象；不能只按方法名称在 H5 链路再猜加一次启动包。
- JADX 1.5.6 使用工作区内校验过下载摘要的便携 Temurin 17，不改系统 Java 8。
- APK 的 `RemoteConfigApiService` 和 `RemoteConfigProtocol` 定义公开配置：[生产 app_config_endfield](https://game-config.hypergryph.com/api/remote_config/105/prod/default/default/app_config_endfield)。实际读取确认：云域名、server app code、passport app code、U8 app code 与已有生产常量一致，排队周期 10000 ms。没有改用 APK 内置的 stable 测试环境默认值。

## APK 约定与适配

APK 约定已经实现为 `GameBridge::Profile::AndroidSdk` 并覆盖 host 检查；第 34–36 轮实际试过 APK 平台/回调组合，仍停在片头。因此当前 libpeer/WebRTC 生产连接明确选择配套的 `Profile::WebRtc`，恢复其 cloud API platform=web、U8 platform=7、派发 cloud_platform=2/is_web=true，以及原网页 gameManager 的回调名称。不能把 Android SDK 的 key 与 H5 传输的 key 混合。APK 确认的独立游戏授权、通道准备时序、JSON data 兼容和诚实的失败响应继续使用。

| 交接点 | APK 证据 | AndroidSdk 模式 |
| --- | --- | --- |
| 游戏启动命令 | `CloudQueueHelper.buildQueueBody` | `cloud_platform=1`、`cloud_is_web=false`，与启动信息保持一致；画质仍采用 720p / 4 Mbps / 30 fps 的既有偏好 |
| 云平台身份 | `HeaderInterceptor` / `OneChannelLoginHelper` | cloud API platform 为 android，U8 ticket platform 为 1；设备型号描述真实 Switch；passport 设备证明使用原已验证身份，不改写为 Android 手机 |
| 业务通道准备 | `CloudBridge.onPipeStateChanged` | 收到 `PipeState=1` 发送 PipelineConnected；首帧仅启动媒体心跳，不再替代业务通道准备 |
| 初始登录信息 | `LoginService.cloudLoginInfo` | 初始为 `{}`，只在 RequestLogin 完成后更新；不能把缓存授权码说成已经完成的游戏登录 |
| 启动信息回复 | `CloudUploadCommand.OnRequestLaunchInfo` | 精确 key 为 `onRequestLaunchInfo`，不是网页端 `OnRequestLaunchInfo` |
| 手柄查询回复 | `GamepadManager.replyOnGetGamepad` | `GetGamepad` 和请求形式的 `GamepadChange` 均回复 `OnGetGamepad`；主动通知仍为全局 `GamepadChange` |
| 授权刷新 | `EnterGameViewModel.refreshHGOauth2Code` / `LoginService.cloudLogin` | 保存账号 + 同账号设备证明执行 CLIENT grant；不在游戏登录刷新中再次兑换 U8/cloud 会话。云分配授权与游戏授权分开 |
| 设备静态属性 | `EventLogService.getStaticPresetProperties` / `EventClass.getStaticProperties` | 使用原生设备/应用/尺寸/渠道字段；APK 渠道为 1/1；不沿用网页的页面、浏览器、SDK 预设属性，不伪造广告、硬件序列号或风控证明 |
| 浏览器桥 | `WebViewService.onCloudBrowserEvent` | Open 的结果 key 为 `OnCloudBrowserEvent`，当前诚实返回不可用；Close 无额外回复；未实现方法返回原请求 key、外层 code=-5，不伪造成功 |
| 未实现命令 | `CloudBridge.onGameDataWithKey` | 原请求 key / eventId + COMMAND_NOT_SUPPORTED(-5)，空 data；不执行支付、网页或确认动作 |
| JSON data 兼容 | `CloudBridge.parseCloudMessage` | data 同时支持对象和 JSON 字符串形式；仍有 64 KiB / 深度 16 的解析上限 |

`local.t=h5`、WebRTC 信令、传输 SDK 版本、Windows VK/虚拟 Xbox 输入描述当前 libpeer 适配的传输协议，不把 APK JNI ABI 当作 Switch ABI。WebRtc 模式使用 `OnRequestLaunchInfo`、`OnGamepadChange`、`OnBrowserOpenResult` 和 Web JS 4.6.1/1 的预设属性；AndroidSdk 模式使用上表。两套契约都有测试，不能把回调大小写差异直接宣称为 H5 链路已证实的 bug。

游戏回复仍有 16 条队列上限、1 秒重试和 15 秒有效期，凭据不进日志。业务确认只证明 pod 收到，不能证明游戏认识回调或完成登录。

## 诊断修复与实测

- 已处理 `swap_gs_info.function.input_ver` 协商。实际 pod 返回 3；`ginput_level=1` 不是协议版本。虚拟 Xbox 在版本 2/3 发送 CtrlMsgV2 完整状态，版本 1 保留旧增量格式；连接/断开仍显式发送。字节与失败重试测试通过。
- 修复一次入口点击被“云端游戏进程已启动”过早禁用的问题。允许操作由已经观察过的片头和独立精确标记控制；5 秒内未能开始就放弃，进入账号/角色/浏览器阶段不再发起，已按下的按钮必须释放。普通使用不自动点击。
- 诊断运行改为显式停止：Endfield 使用 -1，外部 harness 的 0 表示不设运行期限。帧截图为 24 槽环形保留，每 10 秒更新，避免几分钟后只能查看旧图。
- 第 29/30 轮分别实发旧格式 / 协商 V2 的一次 Xbox A，均未推进；第 31 轮在 529 秒结束，没有完成事件，不能据此证明已跨过旧 540 秒期限。
- 第 32 轮（回调名称）：71 秒，解码 1756 帧、错误 0，仍为片头。
- 第 33 轮（独立游戏 CLIENT grant）：197 秒，解码 4929 帧、错误 0，仍为片头。
- 第 34 轮（APK 游戏桥约定）：已确认 Xbox A 在 59 秒发出，仍未见加载条或账号/角色进入。
- 第 35 轮（修正入口点击）：鼠标完整点击在 62 秒发出；明确运行至 587 秒后主动停止，解码 16432 帧、错误 0，仍为片头。`past-old-deadline.json/.log/.bmp` 记录了超过旧 540 秒期限仍在运行且截图继续更新。
- 第 36 轮（完整 APK API/静态属性/通道时序组合）：372 秒，解码 10085 帧、错误 0，收到 5 个业务确认；51 秒入口点击后仍为片头。没有收到游戏 RequestLogin 或账号/角色进入，不能报告为已加载。
- 第 37 轮使用匹配 WebRTC 的桥 dialect，保留 APK 查证的独立 CLIENT grant 和 PipeState 时序。399 秒主动停止，解码 9870 帧、解码错误 0；PipeState=1 与 PipelineConnected 在 29 秒出现，141 秒完整入口点击已确认，仍为片头，没有 RequestLogin / 账号 / 角色进入。退出 cancel 返回 23001，记录为失败，不宣称已释放。构建 SHA-256：`07EBE2F3466040B18E24C688E41FFAF91A105FDF73E0D4411DCC082A8BFB9983`；运行副本与最终构建一致。

从 Android native 库追到 `SendDataToGameWithKey`（0x176f7c）及其 0x241530/0x23e204 路径，参数确为 index / key / 原始 bytes；中间的 `toGS` 是内部路由封装，不是看到一个字符串就给 WebRTC SimpleCmd 猜加字段。网页 SDK 的 SimpleCmd DataToGameWithKey 仍为 arg=index、arg1=key、data=bytes。未据无调用证据的导出符号或字符串发送猜测报文。

相关 host 检查：`endfield_game_bridge`、`endfield_cloud_http`、`endfield_saved_game_authorization` 通过；Switch 0.2.72 完整构建通过。以上不是 Switch 实机体验或实际游玩成功的证明。没有提交、发布或清除保存账号。

最终也复核了 `endfield_vlink_proto`、`endfield_wl_input`、`endfield_xinput_state` 和 PowerShell 监测，均通过。测试进程已结束，独立入口/停止标记归档；保留解码帧与安全事件用于继续查业务初始化，未把正在卡片头的云会话留在后台。剩余加载缺项尚未定位，不能将客户端构建通过或无解码错误视为已解决。

## 本机恢复与 0.2.73 续修

2026-10-25 晚间重新反编译了用户提供的 `Downloads/Programs/EndfieldCloud-1-1.6.0.apk` 与其内置插件，摘要与上述一致。将 APK 独立 CLIENT 与网页模块 61522 的完整浏览器授权刷新实现为可分别测试的策略。实测后尚不能证明浏览器额外 U8/exchange 刷新对已分配 pod 安全，生产默认继续保留 APK 独立 CLIENT；完整浏览器策略没有默认启用，不自动跨策略回退。不会自动同意协议，不改保存账号，不自动另开派发。结果和范围见 [0.2.73 记录](endfield-native-0.2.73-2026-10-25.md)。这些修复尚不能说明卡片头已解决。
