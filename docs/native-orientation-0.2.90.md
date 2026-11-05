# 0.2.90：姿态四元数接入

在 0.2.89 六轴输入基础上补齐原神姿态字段。未修改登录、排队、画质/帧数、
解码、声音、手柄身份或按键映射；终末地仍只发送已确认的重力协议。
本版不是已经验收的体感瞄准成品，没有开展新的在线账号、yuzu 或实机测试。

## 读取与转换

`SwitchMotionSampler` 从同一份 `HidSixAxisSensorState` 读取角速度、加速度和
方向矩阵，借用 Borealis 已启动的掌机/一号完整手柄传感器。不新增线程，
不改变框架的传感器启停和融合配置。

方向矩阵按 libnx 官方六轴示例的三个基向量列解释。沿用 0.2.89 角速度基底
`C*v={v.z,-v.x,-v.y}`，对设备与参考两端换基 `R=C*D*C^T`，再转为归一化的
具名 `w,x,y,z` 四元数。这是基于已有轴映射的实现选择，不是实机标定结论。
HID 的姿态参考不保证指向磁北，也不是重新实现完整 Android 传感器融合。

检查全部元素、列长度、列间正交性和正行列式。零矩阵、NaN/Inf、明显缩放、
剪切或镜像矩阵不产生姿态；容忍小幅舍入误差，输出前检查单位长度。
连续有效采样用内积保持同一四元数半球，避免跨 180° 时仅表示符号跳变。
控制器/传感器切换、断开、时钟倒退或超过 100 ms 的间断清除连续性。

失效矩阵不填单位姿态，也不复制上一帧姿态到新采样；退回原有无姿态六轴字段。
同一个 sampling_number 不刷新时间。保留云端激活门禁、最小 16 ms 发送间隔、
界面/退出/控制器交接暂停和最新采样策略，不以传感器噪声冒充用户活动。
原重力仍为 0.2.89 的低通估计，未在此次改动中改变终末地的重力行为。

## 原神 APK 的实际排列

Android `getQuaternionFromVector` 输出 `[w,x,y,z]`，APK Java 将其放进 JNI 数组
9..12 项。已复查 JNI 入口、Android 转发包装、`sendImuEvent` 和真实 serializer：

| protobuf 字段 | 描述符名称 | APK 实际写入 |
| --- | --- | --- |
| 13 | orientation_x | w |
| 14 | orientation_y | x |
| 15 | orientation_z | y |
| 16 | orientation_w | z |

本版跟随真实 APK 排列，不仅凭描述符名称猜顺序。float32 标签为
`6d / 75 / 7d / 85 01`；零分量按 proto3 语义省略。
外层仍为 command 20010 / type 22 / field 14，未伪造 unbiased 角速度字段。

复查位置（已提供的 yscloud 7.1.0 arm64 库）：

- `ImuEventListener.java` 的 `getQuaternionFromVector` 和数组 9..12 赋值。
- `libclient_core_impl.so`：JNI `0xa9be4` 原样传递数组；Android 包装
  `0xab3b4` 直接转发；`sendImuEvent` 的 `0x86d08..0x86d2c` 将数组偏移
  36/40/44/48 写入消息成员 64/68/72/76。
- `libnetio.so`：`RtcImuEvent::_InternalSerialize` 的
  `0x1a7598..0x1a7624` 将四个成员写成上述标签。

## 新中文安全诊断

沿用约 5 秒汇总，不记录姿态分量、逐采样报文或账号数据：

- `MOTION_ORIENTATION_READY`：有新鲜、通过校验的姿态；0 也可能是界面暂停输入。
- `MOTION_ORIENTATION_REPORTS_SENT`：本地通道接受的含姿态报告累计数。
  失败、过期和未激活不计入；不是云端 ACK 或游戏采用证明。

云端请求为 0 时不会强行发姿态。原有六轴请求、采样、报告和失败统计保留。
终末地没有增加四元数报文或“陀螺仪瞄准”模拟。

## 验证与未验收项

7205 组独立 Rodrigues 旋转检查轴角换基结果，另覆盖显式三轴正向 90°、
180° 附近、舍入误差、无效矩阵、断开/换手柄、冻结采样、失效恢复和半球连续性。
协议固定字节测试覆盖单位姿态及四个不同分量，防止排列、符号和 float32 标签错误。
实际 Session 离线 RTC/WebSocket 两条路径测试包含姿态、服务器开关、发送拒绝和计数；
终末地测试确认即使共享采样含姿态，报文仍只有 gravity。

16 项相关主机测试通过：新姿态、六轴协议、原输入报文、RTC Session、中文日志、
控制器来源、输入捕获、悬浮层、触摸、控制通道、游戏控制、媒体管线，以及终末地
桥、XInput、WeLink 输入/协议。此次未重跑全部 96 项应用测试，不能引用 0.2.89
全量测试作为新版本全量结果。

Switch 生产构建通过，最终 NRO 的 0.2.90 版本检查通过。首轮在链接阶段中断，
留下空 ELF 和旧 NRO，未将其计为通过；确认没有存活构建进程后重新链接成功。
最终 ELF 已核对包含方向转换、采样和 IMU 编码函数，以及两项新诊断事件。
涉及文件的空白检查通过。没有改动第三方库。

最终 NRO：41,073,500 bytes。
SHA-256：`7B7FB20F4EBB176FDD0BAA986A030783B5039703B8A66284FA5F0EB590152AB8`。

真实 Switch 握持方向、轴符号、参考姿态/漂移、不同 Joy-Con/Pro Controller
以及游戏是否消费字段仍需实机验收。数学测试、主机传输模型或成功编译不证明
游戏内体感瞄准已经生效。

## 公开依据

- [libnx 官方六轴示例](https://github.com/switchbrew/switch-examples/blob/master/hid/sixaxis/source/main.c)：方向矩阵列布局、传感器读取。
- [Android SensorManager](https://developer.android.com/reference/android/hardware/SensorManager)：四元数 w/x/y/z 排列。
- [Switchbrew DirectionState](https://switchbrew.org/wiki/HID_services#DirectionState)：方向矩阵分量布局。
