# 0.2.91：原神 APK 对照收包调度 / SD 自定义背景

日期：2026-10-29。基于已有 0.2.90，未更改账号库、画质设置、手柄或体感协议。

## 原神安卓版对照

参考已有用户提供的 `yscloud_7.1.0_3.apk` 提取结果，不代表最新官方版本。
APK SHA256：`AFE345A1561D3A9D3AE6D67B82D95BEDF8E48CCD0C54DA3B5333D55D2F86E209`。

原生库 `libtransport_engine.so` 的字符串/类型信息含 `pc_network_thread`、
`PhysicalSocketServer`、`NackModule2` repeating-task 等，表明原版包含网络线程及
定时补包组件；这不是已还原全部线程实现或已证实每场会话启用 FEC 的证据。
库 SHA256：`F9E418404650847813306D35EDD751F4059D195711148528DA54411A83A867F5`。

本项目实际代码确认：libpeer 在 COMPLETED 分支每次 loop 只接收一个非阻塞数据包。
原神 Session 旧版一轮只调用一次，随后 worker 固定休眠 2ms。关键帧/RTP 突发
容易因读包节奏形成积压，即使网络本身仍有可用带宽。终末地先前已有有界批收，
此次补齐原神；不能据此断言所有实机卡顿都由这个问题导致。

改动：

- 抽出两款游戏共用的有界收包 helper，终末地原有限额/语义不变。
- 已连接媒体在信令前、控制任务后各批收一次；每批最多 64 包或约 2000us。
- 信令、SDK、心跳在两批之间执行，输入仍由原网络 worker 调用，不新建收包线程。
- 非 COMPLETED 的 ICE/DTLS 保持原来一次推进，且先推进再判断握手截止时间。
- 达到批预算时让出调度，不再额外固定休眠 2ms；空闲仍保留 2ms 退避。
- 每包后检查会话错误，回调失败时不继续读完批次；结束后保留最终诊断。

预算是合作式的，不能中断单次慢回调、阻塞的控制连接或系统调度，不能承诺每轮
硬性低于 4ms。预算耗尽只是可能有积压，不等于确认有剩余数据，更不是丢包数。
不移除旧关键帧恢复保护，不改解码队列和画面输出链路。

约每 5 秒及退出前新增中文可读日志：

| 事件 | 意义 |
| --- | --- |
| RTC_SIGNAL_POLL_MAX_US | 信令 poll 会话最大墙钟耗时 |
| RTC_PEER_DRAIN_MAX_US | 单批媒体处理会话最大耗时 |
| RTC_SESSION_TASKS_MAX_US | SDK/控制/心跳等任务会话最大耗时 |
| RTC_RECEIVE_BUDGET_HITS | 累计触达批预算次数，不是丢包数 |
| RTC_PEER_BATCH_PACKETS_MAX | 单批处理包数会话峰值，不是 FPS |

不输出包内容、二维码、登录凭据或现场 SDP。

Java `MihoyoSdkHolder.setBitrate` 将 UI 值乘 1024 后交给 ClientCore。
`setQosPolicy` 依赖选档的 bottom/top 与远端 `BitrateConfig.fluentBottom`，不能只凭
策略名称猜数值。因此这轮不强改最低码率或用户已保存的设置。
`libmedia_engine_receiver.so` 还含 RenderPacingStrategy 的 FPS/掉帧决策方法，
但依赖 Android 显示栈。完整原版 QoS/统计反馈、GCC/TWCC/REMB、FEC、呈现器
没有在本版完成移植，不能描述为“Android RTC 完全复刻”。

## 固定目录自定义背景

SD 卡目录：`sdmc:/switch/SwitchNOW/backgrounds/`。
四个文件名：`genshin`、`starrail`、`zzz`、`endfield`，依次匹配原神、星铁、绝区零、终末地。
各自依次尝试 `.png`、`.jpg`、`.jpeg`（小写），没有跨游戏兜底或网络下载。
同时存在时 PNG 优先；PNG 损坏可继续尝试 JPG。

建议 1280×720，接受最大 4096 边长且总像素不超过 4194304，文件不超过 8 MiB。
只接受实际 PNG/JPEG 文件头；先读有界快照、用实际 stb 探测尺寸，再解码上传同一
份数据，防止探测后重新开文件造成检查/使用不一致。损坏、超限、上传失败均回退。

只在选游戏/重新打开首页时读取，失败结果同样缓存，避免每帧访问 SD。
切游戏先删旧纹理，只保留一张；离开首页释放背景纹理，给串流解码/渲染腾空间。
替换图片后切换游戏再切回，或退出应用重开。图片不影响游戏内画面和悬浮窗。

无自定义图片时直接绘制原创静态几何渐变，不需要 SD、PNG 或下载。
新 NRO 的 RomFS 明确排除原有四张官方游戏图；源工作区历史文件未删除。
可选 PNG 四图包由 `scripts/generate-original-backgrounds.ps1` 生成，采用相同色系与
原创几何构图，不含角色、官方截图、Logo。图包许可单独标注 CC0，不更改程序及
第三方代码/字体的许可证。用户自己的图片权利仍由用户确认。

## 验证边界

真实 devkitPro Switch 构建成功，生成 `SwitchNOW.nro`，38924508 字节。
NRO SHA256：`51CA92BFED44476A0C45159C2452534BE378DB022AABFFA81C46F9027583BBEE`。
检查构建后的 RomFS 及 NRO 文件名，旧四张官方背景均未打入，且新背景/收包诊断已在产物内。

18 个相关 host 目标通过：`home_background`、`decode_recovery_state`、
`endfield_game_bridge`、`endfield_peer_poll_policy`、`endfield_xinput_state`、
`frame_recovery_timings`、`keyframe_request_policy`、`media_pipeline_queue_model`、
`mihoyo_input_packet`、`mihoyo_motion_input`、`mihoyo_motion_orientation`、
`mihoyo_rtc_session`、`mihoyo_sdk_bridge`、`mihoyo_stream_overlay_policy`、
`mihoyo_stream_settings_store`、`mihoyo_ui_language`、`mihoyo_web_control_channel`、`stage_timings`。
Session 测试直接调用生产 Session（网络为受控假传输），验证 31 包突发、每轮两批
上限、持续来包时心跳和输入仍执行、2ms 时间预算、空闲停止、重启统计归零、
握手完成早于截止判断、回调失败当轮停止。并非现场服务端性能测试。
姿态转换测试保持 7205 个旋转用例通过。

背景测试检查固定路径、文件与像素限额、PNG/JPEG 文件头、损坏及上传失败回退、
真实 stb 尺寸探测与解码。额外用同一 stb 解码实际发布的四张 1280×720 PNG，
以及它们的本地 JPEG 测试副本，均成功；JPEG 副本不放入发行包。

13 个既有传输 CTest 二进制重新运行全部通过（DTLS、RTCP、NACK、RTP 重排/组帧/
恢复、socket poll 等）；本轮未修改这些 vendor 代码，也没有重新编译该 host 缓存。
不是全量 host 测试结果。改动文件的 `git diff --check` 通过；全工作树检查有既有
`peer_dtls_setup_test.c` CRLF/空白告警，未混入本次修复。

没有本轮 yuzu、真实登录/排队或实机持续性能验收。
不将 host 测试或成功构建等同于实机不卡/硬解可用。
