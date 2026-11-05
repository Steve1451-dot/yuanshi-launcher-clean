# 0.2.92：仅云原神互换 A/B、X/Y

日期：2026-10-29。用户要求固定四键互换，不增加通用改键编辑器。

## 行为范围

相对于 0.2.91，只在原神 Combo app id 严格等于字符串 `4` 的远程手柄帧中，
互换 physical A/B、X/Y。套用原有 Xbox 布局编码后，云端收到：

| Switch 实体键 | 0.2.91 云端键 | 0.2.92 云端键 |
| --- | --- | --- |
| A | Xbox B | Xbox A |
| B | Xbox A | Xbox B |
| X | Xbox Y | Xbox X |
| Y | Xbox X | Xbox Y |

星铁（8）、绝区零（12）、未知 app id 均不映射。终末地是独立串流分支，
未修改。摇杆、十字键、肩键、扳机、摇杆按下、Plus/Minus 捕获逻辑不变。
掌机两侧 Joy-Con 与其他完整手柄使用同一原神规则。

映射只处理网络发送用的物理输入副本：

1. UI 仍读原始实体键，本地首页与悬浮窗的确认/返回不交换。
2. 原神远程手柄副本执行 A/B、X/Y 互换。
3. 然后叠加已有 Continue/自动进入的模拟 XInput A，避免将它误变成取消键。
4. 原有控制器交接需要中立帧时仍强制发送中立。

启动日志新增 `GENSHIN_FACE_BUTTONS_SWAPPED_AB_XY=1`，中文说明仅远程手柄输入。
这是编进 NRO 的固定规则，重进每次自动执行，不依赖云端是否保存游戏键位，
也不生成或读取新键位配置文件。云端游戏内若又手动交换同一组键，会产生叠加。

保留 0.2.91 原神收包优化、自定义背景、已有登录/排队/手柄/媒体链路。
不改变 Xbox 身份、现有登录库和画质设置，不添加新的体感或 SDK 数据保存功能。
先前未完成的通用 SDK 持久化试验已从本次构建源码撤回，没有写入真实设备配置。
本版也不声称能保存或读取游戏内任意远程键位方案。

## 验证

首轮 7 个相关 host 目标通过：`mihoyo_gameplay_button_policy`、
`mihoyo_launcher_input_policy`、`mihoyo_ui_language`、`mihoyo_input_packet`、
`mihoyo_stream_input_policy`、`mihoyo_controller_source_policy`、`endfield_xinput_state`。

新测试穷举 65536 组物理按钮状态，分别检查原神、星铁、绝区零、终末地标识、
空标识及 `04` 六个 profile，使用实际 XInput 编码验证仅预期面键位变化。
覆盖同时按键、扳机、摇杆、虚拟手柄身份、时间戳、原始 UI 输入不被修改，以及
Continue/自动进入模拟确认不被交换。空输入仍释放全部按钮。

另外 6 项兼容性回归通过：`mihoyo_rtc_session`、`mihoyo_sdk_bridge`、
`mihoyo_stream_overlay_policy`、`mihoyo_motion_input`、`mihoyo_motion_orientation`、`home_background`。
本轮合计 13 项相关 host 目标通过，不是完整全量 host 测试。

真实 devkitPro Switch 构建、链接及 NRO 版本检查通过，版本为 0.2.92。
构建只有既有第三方 stb 初始化告警，未改动 vendor。涉及的已跟踪文件空白检查通过。
最终 NRO 已确认含原神专用互换诊断，以及旧版背景和收包诊断；
没有未完成 SDK 存储试验的路径，也没有把四张官方背景重新内置。

NRO：38924508 字节。
SHA256：`EC25A41F1F388799FC9F1D452FD9CD726B0CE4F3247A62577AD1086B4B8EF7CC`。

没有本轮新的实机或 yuzu 玩法验收，host 测试不是云端游戏实际响应的证明。
