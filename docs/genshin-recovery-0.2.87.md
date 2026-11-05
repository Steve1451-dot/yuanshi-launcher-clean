# 0.2.87：原神丢包/关键帧恢复修正

## 本版范围

本版只推进原神串流的接收恢复，不重写登录、派发、输入或已有协议，也不把
Android SDK 身份、`.so` 或凭据装进 NRO。目标是减少“平时 60 FPS，但丢包或
丢关键帧后冻结 1～2 秒”的情况，而不是宣称已经做到任何网络下零卡顿。

改动依据是 `yscloud_7.1.0.apk` 中已经确认的恢复行为，加上 Switch 自己的
线程、内存和解码约束。**没有把 Android 的线程、缓存或 jitter 数值原样搬
过来，也没有完成整个 Android RTC 内核的移植。**

## 之前为什么会停顿

RTP 层在补包窗口结束、确认一个完整 H.264 access unit（AU）已经损坏时，会
把它丢弃。0.2.86 及更早版本虽然已经知道这个 AU 不能解码，却没有立即把
“画面参考链已经断了”告诉解码管线。上层往往要等到解码进度 watchdog 发现
一段时间没有新画面（约 1.5～5 秒）才请求关键帧，于是用户看到的就是一次
明显冻结。

这不是解码器慢，也不是单纯码率过高；问题在于恢复信号到达得太晚。

## 本版改动

### 1. 损坏 AU 立即上报，不再等 watchdog

- libpeer 新增 `PeerVideoDamage { timestamp, ssrc }` 和
  `onvideodamage` 回调；`rtp_decoder_flush_access_unit()` 在丢弃损坏 AU 的
  同一次调用里触发它。回调只带时间戳和 SSRC，不暴露半截 payload，避免上层
  误把损坏数据送进解码器。
- `Session::Callbacks::video_damage` → `MediaPipeline::notify_video_damage()`
  → `DecodeRecoveryState::mark_damaged()`。损坏 AU 被关闭后，依赖它的后续
  非 IDR 画面立即停止入队，同时马上请求关键帧。
- 这里**不清理已经排队的完整 AU，也不推进 recovery generation、不重置
  解码器**。已经通过 `admit()` 的完整单元仍然有效；损坏字节从未提交给
  FFmpeg。只有真正的显式 resync 才做 generation 递增和 decoder reset。
- 下一个 IDR 到达后，`admit(true)` 结束门禁，恢复同一 generation 的正常
  解码。
- 仍然不放行损坏 AU，不做“花屏掩盖”。如果最后一个包丢失且之后完全没有
  新 RTP，depacketizer 不会关闭任何 AU，这种完全停流仍依赖原有 watchdog；
  本回调解决的是“损坏 AU 后面还有视频”的常见情况。

新增计数：`VIDEO_RTP_DAMAGE_EVENTS`。它统计即时恢复触发次数，不等于丢包
数，也不等于服务器已经回应了关键帧请求。

### 2. 关键帧请求节奏对齐 7.1.0

APK 的关键帧发送侧已确认：最小间隔 250 ms，在一个 PLI 未完成时合并请求，
并存在可选的 35～155 ms 延迟。旧客户端采用 500/1000 ms 退避，单次 IDR 丢失
或服务器忽略一次请求后，解码门禁会保持更久。

本版改为：

- 首次损坏立即请求；
- 在画面真正恢复前，固定每 250 ms 再请求一次；
- 同一时刻只允许一个请求在途，`KeyframeRequestPolicy` 继续限制为最多 4 次/秒；
- 恢复或超过 250 ms 没有新损坏后停止，不周期性刷 IDR。

这只约束“我们发得多快”，**不保证服务器一定按 250 ms 回 IDR**；服务器是否
接受、是否限流仍是服务端行为，本轮没有实机抓包证明。

### 3. 原神补包窗口：512 / 2048 槽，60 ms 预算

原神此前沿用默认的固定 128 槽、30 ms 缺口等待。本版启用：

| 参数 | 值 | 说明 |
| --- | --- | --- |
| 初始槽数 | 512 | 正常 720p 突发不必立刻扩容 |
| 最大槽数 | 2048 | 有界上限，不随断网无限增长 |
| 缺口等待 | 60 ms | Switch 的修复机会预算，不是播放缓冲 |
| 初始 NACK | 16 ms | 与 0.2.86 的自适应恢复一致 |
| 重试间隔 | RTT 或 25 ms，夹在 10～20 ms | 60 ms 窗口内的保守适配 |
| 尝试上限 | 5 次 | 60 ms 到期即停止，通常只能发 3 次左右 |

顺序到达的包仍然立即交付，**没有给所有帧固定增加 60 ms 延迟**。60 ms 是
“等补包的机会”，不是播放队列目标；超过预算仍会丢弃并触发上面的即时损坏
回调。窗口内存约 0.8 MiB 起、最大约 3.2 MiB（每槽按 MTU+256 字节计），
扩容时旧、新分配会短暂共存。

### 4. 原神快照诊断

每 5 秒快照新增以下数值，悬浮窗和自动安全日志都会记录：

| 事件 | 含义 |
| --- | --- |
| `VIDEO_RTP_PACKETS` | 收到的视频 RTP 包数 |
| `VIDEO_RTP_GAPS` | 序号缺口事件数 |
| `VIDEO_RTP_AU_DROPPED` | 补包失败后被丢弃的完整 AU 数 |
| `VIDEO_RTP_DAMAGE_EVENTS` | 损坏 AU 即时触发恢复的次数 |
| `VIDEO_RTP_NACK_REQUESTS / NACK_PACKETS` | 补包请求数据报数、涉及包数 |
| `VIDEO_RTP_NACK_RETRIES / RECOVERED / EXHAUSTED` | 重试、填回、达到上限的包数 |
| `VIDEO_RTP_NACK_SEND_FAILURES` | 本地发送失败次数，不是服务器拒绝 |
| `VIDEO_RTP_REPAIR_MAX_MS` | 缺口发现到填回的最大时间，不是网络 RTT |
| `VIDEO_RTP_TIMEOUT_SKIPS / CAPACITY_SKIPS` | 超时与窗口容量导致的跳过序号数 |
| `VIDEO_RTP_WINDOW_CAPACITY / PEAK / GROWTHS / ALLOC_FAILURES` | 当前/峰值槽数、扩容、分配失败 |

`VIDEO_RTP_NACK_RECOVERED` 只表示“请求后缺口被填上”，不能证明该包一定来自
重传，也可能是迟到的原包。上述计数都不应当直接当成网络丢包率。

## 7.1.0 APK 已确认的参考事实

- 原神官方 SDK 存在 `RtcVqcState`（20035）、`RtcSetQosPolicy`（20023）、
  `RtcSetBitrate`（20019）、`RtcSetBitrateMultiplier`（20039）和关键帧请求
  （20032）；相关 protobuf 结构已从 `libnetio.so` 提取。
- `setBitrateMultiplier` 的合法范围为 `[0.1, 10.0]`。
- 关键帧发送侧最小间隔 250 ms，请求在途时合并，可选 35～155 ms 延迟；
  NACK 上限 1101 个包。相关字符串包括
  `MHYRtcPli deltaframe arrived before a keyframe...`、
  `PacketBuffer full and clear...`、`HandleLoss RequestKeyFrame`。
- 官方接收端包含 NACK、FEC、jitter、GCC 等 WebRTC 机制；本版只落地了与
  当前 SDP 协商和 Switch 延迟预算能对上的 NACK/PLI 部分。

## 没有实现 / 没有验证

- 没有实现完整 FEC、REMB、TWCC 或 GCC。它们是否在当前原神会话中协商、
  以什么冗余布局传输，尚未从实际 SDP/反馈包确认；不能只往 SDP 写名称就
  假装开启。
- 没有复制 APK 的完整 jitter buffer 时限；60 ms 是 Switch 侧预算。官方
  客户端可能等得更久，但直接复制会增加输入延迟。
- 没有新的原神实机、yuzu 或在线会话抓包。不能声称这次已经消除 1～2 秒
  冻结，也不能承诺恒定 60 FPS。
- 硬件解码仍是实验路径；软件解码仍是默认可靠路径。登录、输入、已保存的
  画质/帧率/语言/手柄/解码选择均未改动。

## 验证

- libpeer 主机测试 10 项全部通过：`rtp_reorder`、`rtp_h264_assembly`、
  `rtp_recovery`、`rtp_recovery_alloc_fail`、`peer_dtls_setup`、
  `peer_dtls_receive`、`peer_dtls_loop`、`peer_rtcp_receive`、`rtcp_nack`、
  `rtcp_receiver_report`。其中 `rtp_reorder` 新增断言：补包超时丢弃 AU 时，
  `onvideodamage` 只触发一次，时间戳/SSRC 与损坏 AU 一致；正常重排不触发。
- 应用侧主机回归 93 项全部通过，包含解码恢复状态、关键帧节奏、媒体管线
  队列模型、原神 RTC 会话、语言表、登录/派发/输入和既有终末地测试。
- 新增测试覆盖：`mark_damaged()` 保持 generation、不安排 decoder reset；
  损坏后非 IDR 被门禁、下一个 IDR 恢复；250 ms 固定节奏与 4 次/秒合并；
  `VIDEO_RTP_DAMAGE_EVENTS` 计数。
- 完整 devkitA64 Switch 构建通过，版本检查输出 `0.2.87`；NRO 内可检索到
  `VIDEO_RTP_DAMAGE_EVENTS`、`VIDEO_RTP_AU_DROPPED` 和版本字符串。
- 本机完整 CTest 配置仍受 mbedTLS 生成脚本缺少 Python `jsonschema` 影响；
  上述 10 个相关测试二进制已单独构建并执行，没有跳过失败项。

## 构建标识

- 版本：`0.2.87`
- NRO：`41,057,116` bytes
- SHA-256：`1636EED439A3B4FD65CAAB1FCF6AEB0BC47FF086A7B913CC0AC195B93BC81302`
- 用户数据目录不变：`sdmc:/switch/SwitchNOW/`

正式分发时以外层 `SHA256.txt` 和压缩包校验值为准。
