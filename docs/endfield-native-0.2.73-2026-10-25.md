# 终末地原生适配 0.2.73：2026-10-25 晚间续修

## 本机状态与范围

源码工作区为 `C:/Users/y/Documents/Codex/2026-09-22/da-ji/work/CloudGenshin-NX-native`。G 盘最新代码已同步，但调试产物和反编译目录未同步。本轮只读核对 G 盘第 37 轮安全事件，并将同一加密登录对复制到新的独立 SD 目录复现；不输出凭据，不改原账号存档。没有调用电脑 UI 自动化，没有改手柄数据包、映射或 Joy-Con 选择策略。

用户 APK 的 SHA-256 为 `ABBD3EE37CEBA9FDC627E5AA5DB14850CF4205E495438C692E07F1ECD2CDFB45`。本轮实际重跑 JADX：主 APK 处理 10836 个顶层类，126 个局部错误；插件处理 218 个顶层类，退出成功。不能把局部反编译错误说成全量无误还原。

该 JADX all.jar 的默认入口是 GUI。命令行应使用 `java -cp reference/tools/jadx-1.5.6/lib/jadx-1.5.6-all.jar jadx.cli.JadxCLI --no-res -j 4 -d <输出目录> <APK>`，不要用 `java -jar` 代替 CLI。

## 已实现

1. `SavedGameAuthorization` 显式区分 AndroidSdk / WebRtc 两种授权刷新策略。APK `LoginService.queryLastestOauth2Model` 只刷新 CLIENT code；网页模块 61522 的 `ex()` 则会 CLIENT grant、用同一个 code 兑换 U8、检查已接受的协议、再 cloud exchange，最后提供该 code。浏览器级云会话更新是否必需并不能单由 WebRTC 传输判断：本轮试验尚未证明它能解决片头，且一次试验没有收到媒体，随后旧版对照恢复收流。因此生产保留 APK 独立 CLIENT；完整浏览器策略仅供显式选择的后续诊断，不默认启用。
2. 增加 `AuthorizeWebGame`，每步失败或取消都不提供可用 code，不回落为另一种授权，不改账号，不另行派发或购买时长。保留 109 同账号设备证明恢复限制。
3. U8 协议接口仅 GET 查询，校验 authorized / isLatestUserAgreement。需要新确认时返回本地 -23，并提示先在官方客户端确认；没有 POST 接受协议，也没有反射动态 URL / 账号信息到日志。
4. 排队后游戏授权失败即停止本次启动并经过分配释放保护；不再将无效 loginInfo 带入一个永久等待的片头。失败不会清除保存登录。
5. WebRtc 的失败 `OnLoginResult` 改为官方 `code:2, msg:{code:"",isSuc:false,type:1}`；AndroidSdk 保留其数字错误 msg。两种契约分开测试。
6. 自动日志从 RTC/诊断调用处移到单独写入线程，单个 dirty 标记合并请求，正常写入间隔至少 250 ms；没有快照队列无限增长。退出刷新最后一个 dirty 快照，手动导出仍同步返回结果，原子文件替换和内存日志保留。工作线程创建失败仍可同步回退。
7. 本地启动包入队与服务端 `OnAuthResult` 分开处理：明确拒绝会终止；已发完启动包但 30 秒仍无服务端确认、也没有解码媒体时，报告独立中文超时，不再等到笼统的 120 秒首帧超时。30 秒是本地防御期限，不声称为官方期限；已出媒体的健康会话不会仅因省略该回包而被终止。

APK 新核实的启动调用是插件 `pcgxdq/pcgfzi.java` 6042 分支 → `pcgwqv/pcgcrk.java:143` → native `SendStartGameKey` (0x177264)。JNI 将原始 bytes / 长度 / SDPInfo 交给 native 连接对象，不代表应该把 JNI ABI 当成 Switch ABI，或在既有 H5 PackedData 后猜发第二个启动命令。

## 验证与仍未完成的部分

- 0.2.72 本机基线：156 秒主动截止，收到 4056 个视频单元、解码 3720、解码错误 0；仍停片头，没有账号/角色进入事件。
- 0.2.73 授权链轮次：完整授权返回成功，无需扫码；257 秒主动截止，仍未进入角色。这轮 RTP 序号缺口 3200、拼帧丢弃 2597、解码仅 259，分配释放为安全记录的 DNS 错误 -1006。它不能证明授权调整解决了片头，也不能据单轮有丢包就断言是实体 Switch 的 Wi-Fi 问题。
- 完整浏览器刷新 + 后台日志轮次：旧云会话存档处理 21002 后分配、ICE/DTLS/SCTP 完成，但无 OnAuthResult、音视频全部 0；169 秒以首帧超时终止。不能将此轮称作媒体恢复成功。
- 未改授权链的 0.2.72 对照：127 秒，解码 2565，解码错误 0，RTP 缺口 13；仍未进入角色。这不足以建立云端故障与某个改动的严格因果关系，故不将额外浏览器刷新作为默认修复。
- 恢复 APK 独立 CLIENT + 后台日志 + 启动授权超时保护后的最终复测：157 秒主动截止，视频单元 4133、解码 3388、丢弃 745、解码错误 0；音频解码 13997、错误 0；UDP 收发错误 0，RTP 缺口 50、拼帧丢弃 52。不能将这些计数解释为稳定 30 fps 或无网络丢包。
- 已检查实际解码帧，只有终末地片头；随后在独立精确标记下于 118 秒完成一次入口鼠标点击，结束前仍为片头，没有 RequestLogin / 账号 / 角色进入。普通应用不自动点击、扫码或同意任何页面。测试进程已结束，三个测试标记移入报告归档，未留后台排队。
- 最终轮云 API cancel 仍返回 23001。本地已发送原有用户主动退出命令，但没有服务端释放成功确认，不能宣称云分配已释放；完整退出闭环仍需后续验证。
- 已通过 host：Endfield account policy / cloud HTTP / saved authorization / game bridge / account HTTP / wire proto / vlink proto / input / XInput state / startup queue / peer poll；中文事件检查、SafeEventLog、SafeLogWriter 和 PowerShell 监测通过。SafeLogWriter 包含 10000 次请求、模拟慢盘、最终刷新及保存抛错后继续工作的检查。
- 0.2.73 完整 devkitA64/libnx NRO 构建通过。最终 NRO 40897372 字节，SHA-256 `57489D62EC8FDC22BBE0033D898DE108C0FDCB06EE18C4264E344AED3CE5F845`，与最后隔离运行副本一致。测试 ZIP 仅含 NRO 和 README，不含登录/自动测试标记。浏览器刷新试验副本不作为发布包。

截至以上检查，没有 Switch 实机验证，也没有终末地实际游玩成功的证明。下一步仍需核对游戏初始化/角色进入消息及有丢包时的恢复，不应把编译成功、授权通过、业务确认或片头帧当成完成品。

收尾时 G 盘已不在线，因此未完成源盘与隔离登录文件的最终摘要复核。原盘始终仅申请只读访问；隔离登录仍可用，后续不应因此要求清空或重扫。发布包与最终测试 NRO 的摘要一致，测试自动启动和点击标记已归档关闭。
