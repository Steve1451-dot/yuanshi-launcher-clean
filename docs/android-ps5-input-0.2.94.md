# 0.2.94：原神安卓终端＋PS5 输入实验

本版增加可保存、可关闭的原神专用实验，不代表云端体感瞄准已生效。
保留 0.2.93 的独立键位映射、原神默认 AB/XY 互换和其他游戏输入路径。
未改登录凭据、扫码、鉴权、派发请求、解码、网络恢复和音频策略。

## 使用

1. 用包内 `switch/SwitchNOW/SwitchNOW.nro` 覆盖原文件，保留整个数据目录。
2. 原神首页，等待当前登录恢复/扫码/排队操作结束。点
   「输入：标准模式（点此试用安卓＋PS5）」并确认开启。
3. 按原来的「开始」进入；在游戏内开启需要的陀螺仪/辅助瞄准选项。
   使用 Switch 两侧 Joy-Con 的真实六轴；不需要另外买 PS5 手柄。
4. 不生效、进不去或普通按键异常时，停止并返回首页，点
   「输入：安卓＋PS5（实验，点此关闭）」恢复标准模式，然后重新开始。
   不自动消耗另一次排队或重新扫码。

该开关只在首页切换，下次串流固定使用同一份模式。排队/登录操作中不能切换。
新设置为 `AppHomePath()/genshin-input-mode.txt`，通常位于
`sdmc:/switch/SwitchNOW/genshin-input-mode.txt`。无文件默认标准模式；
64 字节读取上限、格式版本检查、临时文件替换、`.bak` 恢复。
SD 保存失败保留原模式并提示，不假装开启成功。

开关关闭后保留原来的 `cloudgenshin-terminal.txt`、`cloudgenshin-pad-id.txt`
手动覆盖；这些文件不删除、不重写。如果以前自行设置过覆盖，标准模式会继续使用它们。
实验开启时两者不覆盖实验组合。键位配置、登录 vault 和其他游戏均不变。

## 实际做了什么

- 串流 `StartGameReq` 和 `SdkDeviceInfo` 使用 APK 枚举 `CTT_ANDROID=3`。
- 桥 `info_get_cps` 使用现有的 `android_mihoyo` 分支；启动页面使用现有触摸路线。
- `RtcGamepadList` 和每份 type-17 手柄状态都使用 DualSense `VID=054c / PID=0ce6`。
  仍然发送 APK 的 XinputData 按键/摇杆语义，不伪造 USB HID DualSense 原始数据。
- 六轴继续走原有 type 22 / field 14 安卓手机 IMU，包含已接入的姿态四元数。
  只在已授权会话收到合法 type 23 反馈开启后发送；不强行制造激活反馈。
- 实验会话内暂停本地「体感转右摇杆」，不修改保存的辅助开关、灵敏度和按键配置。
  悬浮窗和映射页标明暂停状态。停止、UI 捕获、过期采样和手柄来源交接保护保留。
- 云端 `gamepadEnablePs` 关闭时记录事实；原有虚拟手柄强制上报策略保留，
  不把本地提交成功误报为云端接受。

这不是完整 Android 系统模拟，也不是宣称全套安卓身份已复刻：
前置登录/HTTP/测速/派发仍沿用当前实现，SDK 版本沿用该游戏现有配置，
设备信息继续如实填写 Nintendo Switch / Horizon OS。
该版本只验证“安卓串流终端＋PS5 身份＋独立手机 IMU”这一组变量。
如果仍未激活，需要继续核对前置请求、游戏控制模式和服务器事件，不能因此断言不支持。

## 自动中文安全诊断

沿用每约 5 秒的自动日志和容量限制，无新增逐帧日志、原始六轴、二维码或账号数据。

| 事件 | 含义 |
| --- | --- |
| `GENSHIN_ANDROID_PS5_EXPERIMENT_ACTIVE` | 本次采用实验组合，不是体感生效证明 |
| `STREAM_TERMINAL_TYPE` / `GAMEPAD_IDENTITY` | 实际终端值与设备 VID/PID |
| `GAME_CONFIG_PS` | 云端返回的 PS 手柄配置，不等于设备已接收 |
| `MOTION_ACTIVATION_RECEIVED` | 已就绪会话收到的开启请求数，短暂开启后关闭不丢历史 |
| `MOTION_FEEDBACK_BEFORE_AUTH` / `INVALID` | 会话未就绪的反馈或未通过校验的反馈 |
| `MOTION_SAMPLE_FRESH` / `ORIENTATION_READY` | 当前六轴和姿态可用，不是游戏采用 |
| `MOTION_REPORTS_SENT` / `ORIENTATION_REPORTS_SENT` | 本地通道接受的报告数，不是云端 ACK |
| `MOTION_ANDROID_PS5_NO_ACTIVATION_20S` | 输入就绪约20秒仍未识别开启，单次提示，不停止会话 |
| `MOTION_ANDROID_PS5_FIRST_LOCAL_SEND_NOT_ACK` | 实验首次本地提交成功，单次证据 |

开启请求一直为 0：先查终端/游戏设置/激活路线，不归咎于 Joy-Con 没传感器。
开启请求大于 0、采样可用但发送为 0：查输入捕获、会话就绪和本地发送。
发送增长但瞄准无反应：仍需核对游戏是否采用、坐标方向和控制模式，不能直接宣布成功。

## 验证边界

20 项不同的相关主机测试通过：`genshin_input_experiment`、`controller_mapping`、
`controller_mapping_editor`、`controller_mapping_draw`、`gyro_stick_assist`、
`mihoyo_motion_input`、`mihoyo_motion_orientation`、`mihoyo_rtc_session`、
`mihoyo_ui_language`、`mihoyo_game_control`、`mihoyo_input_packet`、`mihoyo_game_profile`、
`mihoyo_launcher_input_policy`、`mihoyo_sdk_bridge`、`mihoyo_stream_input_policy`、
`endfield_xinput_state`、`mihoyo_login_store`、`mihoyo_qr_login`、
`mihoyo_passport_login`、`mihoyo_launch_flow`。

新增测试覆盖有界文件解析、保存/重读/关闭、损坏主文件和备份、显式关闭覆盖旧备份、
写入失败、不影响其他游戏、65536 个按键掩码的 PS5/Xbox 语义一致性、
不篡改原辅助偏好、等待激活/首发事件/重复抑制/就绪中断和时间倒退。
真实 Session 实现的离线传输测试确认 StartGame 和 SdkDeviceInfo 都带终端 3，
设备列表及实际按键帧都带 PS5 VID/PID，六轴在合法激活前不发送，激活后编码、
发送失败、关闭和停止重置正确。测试输出中的 fake transport error 是受控失败分支。
中英绘制命令模型确认实验暂停说明，不是实机视觉验收。

生产 Switch 完整构建通过（125 步，诊断改动后再 14 步增量构建），版本检查 0.2.94。
只见原有第三方 stb 初始化警告；未修改第三方库。本轮涉及文件 diff 空白检查通过。
最终 ELF 已确认包含新实验事件、体感激活计数和 0.2.94 版本。

隔离 yuzu 的掌机探针也通过，使用新的真实 libnx SD 保存和切回代码：

```
ANDROID_PS5_PROFILE_SD_ROUNDTRIP pass=1 rolled_back=1 cloud_packets_sent=0
MOTION_PROBE_DONE pass=1 fresh=1483 moving=1012 poses=1483 pose_changes=1290 local_encoded=988 encode_errors=0 gate_off_blocked=495 gate_errors=0 cloud_packets_sent=0
```

记录位于独立环境 `reports/android-ps5-094-probe.log`。
探针使用模拟六轴与本地反馈，不登录、不排队、不向云端发送。
没有进行生产 NRO 的新在线登录或真实 Switch 验收；上述结果不能替代
云端激活、游戏实际采用、瞄准方向与手感验收。

最终 NRO：38,944,988 bytes。
SHA-256：`0FBC3898A988CDAC31BDE98AFBADFFD394C6F46C2EF31023F2B65F819BB9EB72`。
冻结的 NRO 和匹配 ELF 保存在工作区 `outputs/android-ps5-0.2.94/`，不含账号数据。
