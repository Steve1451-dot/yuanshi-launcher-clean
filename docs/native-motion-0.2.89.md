# 0.2.89：原生六轴输入实验接线

本版继承 0.2.88 的恢复/调度修复，不修改解码器、音频、排队和登录存储。
不是已经验收的陀螺仪瞄准成品。没有运行新的在线账号、yuzu 或 Switch 实机测试。

## 已接入

- Switch 掌机模式（机身两侧 Joy-Con）、一号完整手柄/成对 Joy-Con。
  借用 Borealis 已启动的六轴传感器，只读取状态；退出串流不停止框架的传感器。
  不把别的玩家的六轴混入当前控制器。二号到八号手柄仍能按键操作，但本版不读取其六轴。
- 原神：APK 7.1.0 的 `RtcDataChannel` 20010 / type 22 / field 14；
  发送角速度（rad/s）、加速度（m/s²）、负单位重力以及屏幕旋转 1。
  只在收到 type 23 / field 15 的 `imu_state_activated=true` 后发送；
  信令代理、选中的 RTC 控制通道和独立控制 WebSocket 共用同一开关。
  不修改 Xbox 手柄身份，不把体感模拟成摇杆或鼠标，不以传感器噪声冒充玩家活动。
- 终末地：APK 的 `SensorSwitch` 请求开启 `gravity` 后，发送
  `HighFqDataToGame` / `SensorEvent`，全局事件、eventId=0、code=0，
  data 内是单位化的 `gravity{x,y,z}`，按 APK 横屏左转规则调整坐标。
  不加入可靠业务 ACK 重试队列，不改变现有 Web/Windows 启动兼容身份。
  这证明接上了重力协议，不证明该游戏支持“陀螺仪瞄准”。
- UI 只发布最新采样，沿用现有输入锁和网络 owner，不增加线程或历史队列。
  原神发送间隔至少 16 ms；终末地至少 34 ms，重力变化阈值 0.001。
  超过 100 ms、未来时间、重复采样、非有限数以及断开来源均不能发送。
  本地发送失败不重放旧采样，下一份新采样仍受间隔限制。
- 悬浮窗、诊断、退出和输入交接期间暂停六轴；恢复时不抢在必要的中立手柄报告之前。
  保留原来的按钮、摇杆、触摸和退出方式。

## 尚未证明/未完成

HID 没有直接提供 Android TYPE_GRAVITY。本版用可信的加速度低通估计重力，
遇到自由落体/大冲击不初始化估计，采样中断或控制器变化后重置。
它不是 Android 原版四传感器融合算法。没有伪造 TYPE_GAME_ROTATION_VECTOR，
IMU 的 orientation_x/y/z/w、unbiased 角速度字段均未接入；需要后续验证云端
是否依赖这些字段，以及用可靠的方向矩阵转换接入姿态。

坐标映射以现有 Borealis/Moonlight 横屏基底为参考，再转换为 Android
自然竖屏传感器坐标。符号、握持方向、漂移、不同手柄以及游戏是否消费消息
仍需实机定向验收。采样跟随 UI 轮询，不保证 60 Hz 独立采样率。
云端若不请求传感器，报告数为 0 是预期行为；不会强行启用或改发未经证实的陀螺仪消息。

## 安全日志

沿用约 5 秒汇总，无逐采样日志，不记录六轴原始值或账号数据：

| 事件 | 含义 |
| --- | --- |
| `MOTION_SERVER_REQUESTED` | 1：服务器要求传感器；0：未请求/已关闭 |
| `MOTION_SAMPLE_FRESH` | 当前有可发送的新鲜采样；0 也可能是本地界面捕获输入 |
| `MOTION_REPORTS_SENT` | 本地传输接受的报告累计数，不是游戏接收确认 |
| `MOTION_SEND_FAILURES` | 实际尝试发送但本地拒绝的累计次数 |

实机观察应先看云端请求，再看采样可用，最后看提交数是否增加，不能仅凭 NRO
成功编译或 yuzu 中普通按键正常就认为真实传感器可用。APK 的游戏内体感开关
如果需要，仍应由用户在游戏设置里开启。

## 研究依据

- [Moonlight-Switch InputManager](https://github.com/XITRIX/Moonlight-Switch/blob/master/app/src/streaming/InputManager.cpp)：
  使用 Borealis 的 ACCEL/GYRO 回调，GYRO 在发送给 Moonlight 协议时转为 deg/s。
  本项目原神协议需要 rad/s，不能照搬其乘数和 Sunshine 配置。
- 仓库 `extern/borealis/.../platforms/switch/switch_input.cpp` 的原始 HID 轴映射和
  传感器生命周期；本次未修改 vendored Borealis。
- 已提供的 yscloud 7.1.0：`ImuEventListener.java`、`ClientCore.java`、
  `libnetio.so` 的 common/datachannel.proto 描述符以及
  `ClientCoreImpl::sendImuEvent` 实现。
- 已提供的 Endfield 1.6.0：`GravitySensorReporter.java`、
  `HGCloudGameSdk.sendHighFqDataToGame`、`PlayGameViewModel.onSensorSwitch`；
  本地官方 H5 bundle module 6718 的 `Ny(cmd,arg,data,arg1,arg2)` 和
  `sendHighFqDataToGame` 的封装。未把安卓 .so 直接打入 Switch。

## 验证记录

新增协议/策略测试覆盖角速度单位转换、横屏重力转换、浮点字节编码、畸形反馈、
请求开关、过期/重复采样、失败不重放和 UI 捕获。原神 Session 使用现有离线
传输模型验证 RTC 和独立控制 WebSocket 两种发送路径；终末地验证真实 GameBridge
序列化及无可靠队列积压。它们不是真实云端会话或物理六轴验收。

实际执行结果：95 项应用侧主机测试全部通过（含新六轴测试、实际 Session
的离线 RTC/WebSocket 路径、现有登录/输入/媒体管线回归）。Switch 完整构建通过，
输入交接保护补齐后再次增量编译并链接 NRO，通过 0.2.89 版本检查。
构建仅有原有第三方 stb 初始化警告；本次涉及文件的 diff 空白检查通过。
最终 ELF 中确认包含两款游戏的 send_motion、反馈解析和重力消息构造函数。
这些都不能代替真实传感器、云端采用和游戏内方向验收。

最终 NRO：41,069,404 bytes。
SHA-256：`D8887D10F94407A4603FC5B794C938B927F44057A16CBFCD647218A9C9067430`。
