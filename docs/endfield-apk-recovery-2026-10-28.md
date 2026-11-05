# 终末地 APK 原生收包/恢复调查（2026-10-28）

后续已落地的 0.2.86 接收恢复候选见 `endfield-recovery-port-0.2.86.md`。
下文记录研究当时的 0.2.85 基线，不表示 0.2.86 已完成全部 APK/GCC/FEC 移植。

## 本轮结论与边界

建议是**对齐已证实的协议与恢复语义，针对 Switch 重新实现平台调度**，而不是
照搬整个 Android 内核，也不是继续只调码率/超时猜效果。

本轮只增加离线研究工具、工具测试和本记录，没有改动运行时或发布新 NRO。
当前产品版本仍为 0.2.85；未运行 APK、登录、排队或进行新的实机/yuzu 验收。
以下地址均为 ELF 虚拟地址，不是文件偏移。反汇编中显示的最近导出符号
`opus_multistream_decode_float+...` 不是这些内部函数的真实名称，不能据此
把视频代码误认作 Opus。函数边界以 `.eh_frame` FDE 另行核对。

## 样本与复现

原先 Downloads/Programs 下用户给出的外层 APK 当前不在该路径；使用工作区此前
保留的内层公开插件，不把旧文档中的外层 APK 哈希冒充本轮重新验证的哈希。

- 内层 `reference/android/endfield-cloud-1.1.6.0/assets/wl_game.apk`：
  SHA256 `bc59ea36ecf1dbfed3376b8243b1bf06a141cdab9f798f79a31df94ddea8bb3d`。
- ZIP 内 `lib/arm64-v8a/libwlcgcore.so` 与工作区已提取文件逐字节哈希一致：
  5,410,624 bytes；SHA256
  `512341992a6762b8260e594992c80fb84f0c3d3a2d9e97552a32b0420397eaf9`。
- Build ID `963143f4eec57f575b1c1a68f12f35bcd493ee18`；ELF64 little-endian AArch64。
- `.rodata` VA/文件偏移均为 `0x51e80`；`.text` VA `0x173480`，文件偏移
  `0x172480`。不能对代码地址直接按同数字文件偏移读取。

新增 `scripts/inspect-apk-native.py`：只读取 ELF 的只读字符串及代码指令，定位
ADR、ADRP/ADD 候选引用和直接 BL 调用，不执行 APK，不读取任何账号文件。
ADRP/ADD 查找是有界候选扫描，不是完整控制流/寄存器活性分析；以下重要引用均
再用 devkitA64 objdump 人工确认。虚调用不能仅靠该扫描确定动态目标。

从仓库目录运行（MSYS Python 的脚本路径使用 `/`）：

```powershell
& C:/devkitPro/msys2/usr/bin/python.exe -B tests/inspect_apk_native_test.py -v
& C:/devkitPro/msys2/usr/bin/python.exe -B scripts/inspect-apk-native.py ../../reference/android/endfield-cloud-1.1.6.0/libwlcgcore.so --strings 'NACK list full|removed from NACK|Clear PacketBuffer'
& C:/devkitPro/msys2/usr/bin/python.exe -B scripts/inspect-apk-native.py ../../reference/android/endfield-cloud-1.1.6.0/libwlcgcore.so --calls-to 0x414068
& C:/devkitPro/devkitA64/bin/aarch64-none-elf-objdump.exe -d --start-address=0x414068 --stop-address=0x414398 ../../reference/android/endfield-cloud-1.1.6.0/libwlcgcore.so
```

## 1. 已确认：NACK 不是只有一个“请求关键帧”开关

| 位置 | 直接观察 | 能得出的结论 |
| --- | --- | --- |
| `0x413e70` 构造函数，`0x413ee8–0x413f04` | 写入 100000 和 16000 时间数值 | 此对象默认时间配置为 100 ms 和 16 ms，单位依据见下 |
| `0x414068–0x414398` 批处理 | 检查初次时间、上次发送时间、序号及选项 | 有按序号和时间筛选的补包批次，不是发现缺口就无条件清队列 |
| `0x414128–0x414150` | 初次等待读取对象 `+168`，再次等待比较 100000 | 该函数初次门限默认 16 ms，后续时间重试门限为 100 ms；不是当前运行中测得的 RTT |
| `0x414224–0x414238` | 旧重试数加一，旧值小于 4 时保留，否则走删除 | 本样本该批处理分支最多选择同一缺失项 5 次 |
| `0x414280/0x414284` | 引用重试耗尽日志，之后删除条目 | 5 次耗尽是移除 NACK 项，不等于该处直接请求 IDR |
| `0x4147e4–0x414b38` | 数量超过 1000，调用 `0x414e0c` 尝试清理并重算 | 不是一到上限就立即清空；清理仍不足才进入最终恢复 |
| `0x4148d8–0x414944` | 清 NACK 列表、记录满列表日志并虚调用 keyframe sender | 超限无法缓解时请求关键帧 |

时间单位追踪：`0x414094` 调用 `0x1ae414`，该处通过重定位表项
`0x52af68 -> 0x187320` 取当前时间。`0x187320` 使用 `clock_gettime`，秒乘
1,000,000，纳秒换算微秒，故 16000/100000 分别为 16/100 ms。

存在独立周期批处理：`0x413dc8–0x413e70` 遍历注册模块，`0x413e00` 调用
`0x414068`，再虚调用 NACK sender；收包路径 `0x4146a8` 也调用同一批处理。
这证明存在不依赖“新视频包到达”的补包入口；本轮没有确认周期调度间隔、
所有运行时配置覆盖或官方当前会话是否使用此分支。

上游 [NackRequester 源码](https://webrtc.googlesource.com/src/+/refs/heads/main/modules/video_coding/nack_requester.cc)
可用于对照职责分工，但不是 APK 同版本源码。检索日 main 的
`kMaxNackRetries=100` 与样本汇编的 5 不同，不能把 main 的常量当成 APK 结果。

## 2. 已确认：包缓冲容量与清空条件

- 视频接收器构造路径 `0x40ef74`，在 `0x40f194–0x40f1a0` 传入
  `start=512`、`max=8192`，调用 `0x415810`。构造函数将 max 存入对象，
  并按 start 分配指针数组。这里是 **RTP 包槽位，不是缓存 8192 帧**。
- `0x415998–0x415da0` 插包路径发现槽位冲突后，先调用 `0x415da0`
  扩容；后者扩大容量至旧值两倍但不超过 max，并重新映射已有包。
- 扩容后仍冲突才在 `0x415af4` 引用清缓冲日志，调用 `0x416038` 清理，
  设置返回结果 `buffer_cleared` 字节。重复包有单独返回分支。
- 该调用点的 NACK 对象是否创建还受 `0x40f150–0x40f180` 的配置判断控制。
  不能声称任何模式下一定开 NACK，也不能把 8192 槽视为实测占用。

对照上游 [PacketBuffer](https://webrtc.googlesource.com/src/+/refs/heads/main/modules/video_coding/packet_buffer.cc)
可确认扩容、缺包跟踪和帧组装的职责，但具体样本容量来自上面的汇编实参。

**不能直接把 Switch 的 128 改为 8192**：当前 libpeer 使用固定整包槽位，不是
同一种按需分配的 packet 对象；还必须约束时间、内存和追赶延迟。重传包缓冲与
解码/显示队列是不同层，不能以“加包槽位”为由扩大后两者。

## 3. 已确认：官方 H.264 同样不随意跨越未知缺口

`0x4163a8–0x416d0c` 是组帧主体。它识别 SPS/PPS/IDR 并填写 key/delta 类型；
`0x4167e0–0x416820` 的关键帧分支跳过后续缺包检查。普通帧则在
`0x416820–0x4168a8` 查询缺失序号集合；集合在目标起始序号以前存在缺口时，
转入不交付该批帧的路径。结果与上游 H.264 缺口检查结构相符。

`0x417cec–0x41814c` 的引用跟踪分支还比较帧首尾序号与最近连续序号；
`0x4257f0–0x4258d8` 在可解码条件消失时取消当前交付，有相应日志引用。

因此本轮**没有证明“官方总能通过丢几帧跳过损坏参考帧继续解码”**，也没有证明
它在这里使用 `nal_ref_idc=0` 进行安全丢弃分类。此前的非参考帧分类建议仍是
需要独立设计/证实的优化，不是已经从这个 APK 还原出的算法。

## 4. 反馈、码率和 FEC：已找到代码，不冒充已启用

码率调用链与现有实现一致：

- 内层 Java `pcgnsg/pcgkgp.java::pcgnrm(int,int)` 验证 min <= max，按旧 floor
  决定先写 ceiling 还是 floor；`WLCGStartService.java:1119/1185` 分别转发
  `autoBitrateAdjust` 与 `setBitrate`。
- `pcgxdq/pcgfzi.java:1888/2098` 转为 `AndroidCmd`；native `0x241edc`
  构造 `autoBitrateAdjust` 命令并在 `0x241f58` 调通用发送器。
- 当前 `app/src/endfield/stream_settings.hpp` 已实现相同范围和顺序语义。
  这条链只证明设置命令，不证明本地带宽估计或服务器真的采纳了目标值。

比孤立字符串更强、但仍不是运行证据的发现：

- `0x32f818–0x32f8b4` 构造并加入 `goog-remb`、`transport-cc` 反馈参数。
- `wlfec-01` 不只存在于字符串表：`0x3b3a24–0x3b3a50` 实际比较名称并映射
  codec 分类值 4，`0x42bb00` 附近按 codec 名称检查能力列表。
- 原生 FEC 初始化 `0x38cb28` 调用 `0x38cd20`，并有独立编解码相关代码。

未证实：本次官方移动会话协商了哪一种 FEC、实际冗余布局、恢复成功率、
反馈扩展 ID、服务器对 H5 路径的兼容能力。不能只往 SDP 添加名称就宣称有 FEC
或拥塞控制，也不能把 Moonlight 的包格式和服务器策略混入 WeLink。

当前 Switch 路径 `extern/libpeer/src/sdp.c:29` 只为视频生成 nack/nack pli，
没有 REMB/TWCC/FEC 协商和相应完整算法；但已有 RTCP Receiver Report，**不是
完全不向服务器反馈**。基础 RR 不等同于完整带宽估计闭环。

## 5. 与当前 Switch 实现的具体差异、后续顺序

| 层 | 当前 Switch | 后续可验证工作 |
| --- | --- | --- |
| NACK 调度 | `peer_connection_maybe_send_video_nacks` 仅从新视频包接收分支调用，同一预期序号最短 10 ms | 把补包调度做成独立有界状态，测试尾部停包仍能重试、重复包不产生反馈风暴 |
| 可补包窗口 | 固定 128 槽，终末地 80 ms；0.2.85 已修复新缺口被旧缺口提前淘汰 | 用突发/补传延迟矩阵确定容量与截止时间；分清容量淘汰、时间到期和真正无补包 |
| 恢复语义 | 损坏 AU 触发 resync，后续非 IDR 丢弃 | 保留未知/参考损坏保护；先证明哪些损失不影响参考链，再单独放宽，不能直接关门禁 |
| 带宽控制 | 已发送码率上下限，已有 RR；没有完整 REMB/TWCC | 先确认服务器可协商反馈种类，再补收包时间/损失反馈与有冷却的码率闭环 |
| 平台调度 | 信令及 peer 处理仍可能共享轮询预算 | 区分阻塞与 CPU 调度，保证收包优先和已有线程所有权；不能把 APK Android 线程原样搬入 |

**不能抄 100 ms 重试间隔却仍只等 80 ms**，否则首次补包没回来时，第二次
请求前包已经过期。也不能只扩大缓存却保留无界等待或让控制连接饿死收包。
这些参数必须以一个有界恢复策略联合测试，而不是逐项凑成“同款”。

下一步优先项：独立补包时钟及重传年龄/有效补回诊断，然后做定量突发测试。
FEC 和完整拥塞反馈另需协商及格式证据；它们不是本轮已完成的功能。

## 验证

`tests/inspect_apk_native_test.py` 的 10 项合成 ELF 测试通过：VA/文件偏移分离、
ADR 负偏移、ADRP/ADD、shifted ADD、寄存器匹配、调用/返回边界、page 替换、
窗口上限、正反 BL、错误/截断文件。实际样本关键 xref 在加边界检查后重跑一致。

这些是**研究工具正确性测试**，不是串流性能或实机通关证明。没有据此改产品
版本、要求用户换 NRO、关闭损坏保护、改变认证身份，亦未输出任何运行凭据。
