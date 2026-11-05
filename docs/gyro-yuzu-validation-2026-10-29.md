# 陀螺仪 yuzu 验证记录（2026-10-29）

结论：模拟运动经过 yuzu、libnx、生产采样/姿态转换和 IMU 编码的本地链路可运行。
这不是云游戏内体感已生效的结论。本轮没有登录、排队、发送云端体感数据或实机标定。
生产版本仍为 0.2.92，没有修改游戏客户端代码或发布新的 NRO。

## 已执行

在工作区 `.opennow-msys-tmp/yuzu-motion-probe/` 创建独立 portable yuzu，
使用本机 EA 4176 Optimized 的程序和 DLL，不复用原模拟器的 SD、配置或账号文件。
按用户本轮指定，仅复制其本机 `prod.keys` 到独立模拟器目录；没有显示密钥内容，
没有将其放进报告、源码或发布包。没有使用截图、按键注入或鼠标自动操作。

独立 `switch_motion_probe.nro` 复用以下生产代码：

- `SwitchMotionSampler` 和 `MotionTracker`，含原来的坐标/单位转换、四元数和新鲜度检查。
- `DecodeImuFeedback`、`MotionSendPolicy`、`EncodeImuChannel` 与输入报文封装。
- 传感器由探针本身启动；生产客户端仍由 Borealis 管理，未修改其生命周期。

模拟运动通过仅监听 `127.0.0.1:26761` 的 DSU 输入提供。
输入先静止，再三轴运动，再静止。原生编码开关由明确标注的**本地反馈夹具**开启，
不是服务器回包；所有本地编码帧均留在探针内，`cloud_packets_sent=0`。

每轮约 24 秒，统计日志每秒写一次。有效姿态表示通过现有矩阵/单位四元数校验，
不表示握持轴向已标定，也不表示姿态等于物理传感器真值。

| 模式 | 实际 HID 来源/样式 | 有效新采样/姿态 | 运动采样 | 姿态变化 | 本地编码 | 编码/门禁错误 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| Pro Controller | No1 / 1 | 1473 | 925 | 1286 | 981 | 0 / 0 |
| 双 Joy-Con | No1 / 4 | 1473 | 919 | 1283 | 981 | 0 / 0 |
| 掌机（对应两侧 Joy-Con 的来源） | Handheld / 2 | 1481 | 927 | 1285 | 988 | 0 / 0 |

原始报告分别位于独立目录的 `reports/pro-keys-probe.log`、
`joydual-probe.log`、`handheld-dsu-probe.log`。

随后给探针加上“必须跑满 24 秒”保护并重新构建，再测掌机模式仍通过：
1480 份有效姿态、925 份运动采样、1292 次姿态变化、989 次本地编码，
编码/门禁错误均为 0。最终记录在 `reports/handheld-final-probe.log`。

首次无密钥启动超过自检等待时间，没有得到探针结果，没有计为通过。
掌机配置的前两次失败也保留：第一次掌机槽位未连接；第二次已连接但运动仍绑定键盘。
补齐 **player 8 掌机槽位**，消除被旧键盘条目覆盖的运动绑定后通过。
这两项是独立模拟器测试配置问题，不是对生产 Switch 代码的修复。

相关三项主机测试重新执行通过：`mihoyo_motion_input`、
`mihoyo_motion_orientation`（7205 组旋转），以及包含体感回归的 `mihoyo_rtc_session`。
会话测试使用离线传输模型，输出中的 fake transport error 是受控失败分支，不是真实服务错误。
独立探针通过实际 devkitA64/libnx 编译并生成 NRO；本轮未重新构建生产客户端。

## 与实机现状的区别

上轮已从用户 Switch 读取的 0.2.91 安全日志仍显示：

```
t=468s MOTION_SERVER_REQUESTED code=0
t=468s MOTION_SAMPLE_FRESH code=1
t=468s MOTION_ORIENTATION_READY code=1
t=468s MOTION_REPORTS_SENT code=0
```

因此硬件读取不是当时的直接阻断点：本地六轴和姿态可用，但客户端没有识别到
开启原生 IMU 的请求，发送次数为 0。不能把它简单归为传感器坏了，也不能反过来
断言服务器完全没有发过其他形状的请求。下一步应核对实际会话的终端/控制模式、
游戏内体感开关、服务器激活反馈和游戏采用路径，而不是未经验证地强制发报文。

未验收：真实云端激活/接收、游戏内效果、Joy-Con 握持方向与轴符号、灵敏度、
漂移，以及长时间物理运动表现。终末地原有重力协议没有在本轮做云端验证。

0.2.92 已发布 NRO 的 SHA-256 本轮复核未变：
`EC25A41F1F388799FC9F1D452FD9CD726B0CE4F3247A62577AD1086B4B8EF7CC`。

## 复现入口

- 构建：在 devkitPro MSYS2 中，以 `DEVKITPRO` 指向安装目录，运行
  `cmake -S tests/switch_motion_probe -B build/motion-probe -G Ninja`，再构建该目录。
- DSU 自检：`python scripts/dsu-motion-probe.py --self-test`。
- 运行：`scripts/test-switch-motion-yuzu.ps1`，需要指定 `IsolatedRoot`、`ProbeNro` 和
  `PythonExe`。根目录名必须以 `yuzu-motion-probe` 开头；脚本拒绝已有账号 vault 的测试 SD。
- 独立 portable 配置启用 motion，UDP 输入为 `127.0.0.1:26761`；运动绑定为
  `motion:0,pad:0,port:26761,guid:0000000000000000000000007f000001,engine:cemuhookudp`。
  绑定左右 motion 并设其 `\default=false`。Pro/双 Joy-Con 分别使用 player 0 type 0/1；
  掌机 type 4、非底座模式，并需连接 player 8、配置其运动来源。INI 不要重复相同键。
- 探针日志在独立 portable `user/sdmc/switch/SwitchNOW/motion-probe.log`。
  不能把此诊断 NRO 当作云游戏客户端，不能覆盖用户正常 SD 的 `SwitchNOW.nro`。

DSU 字段和单位依据 [Cemuhook 协议规格](https://v1993.github.io/cemuhook-protocol/)。
本轮没有修改 yuzu 或任何第三方库源码。
