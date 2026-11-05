# CloudGenshin-NX 命令行自动调试（2026-10-16）

2026-10-31 更正（0.2.96）：EA4176 的 `yuzu.exe` 不会把 `-c` 当作全局
配置文件覆盖。此前独立配置示例不能套用到 GUI 版。GUI 必须使用专属 portable
目录里真实的 `user/config/qt-config.ini`，并在 `[Data%20Storage]` 中显式设置
`sdmc_directory` 和 `sdmc_directory\default=false`。脚本现在启动前校验配置路径及
SD 路径相同，不再将“程序跑在另一张 SD”误报为没启动。
下面 `yuzu-cmd.exe` 示例是历史记录，不能作为这次 GUI 参数已验证的证明。
当前隔离环境与失败证据见 `ps5-startup-debug-0.2.96.md`；没有操作用户日常配置。

不用电脑操作技能、鼠标、OCR 或截图判断登录状态。程序收到真实登录成功结果后
自动普通排队一次；PowerShell 只读取安全日志和固定 GDB 标记，不监控秘密内容。

## 本机已验证的链路

0.2.11 的即时调试已跨过原来的超时和视频为 0：

- 空网卡配置导致本地候选为 0、候选对为 0，UDP 检查根本没有发送。
  只把独立 yuzu-debug.ini 的 Services/network_interface 改为实际联网的 WLAN 后，
  本地候选恢复为 1，并收到通过认证的连通性应答。不修改用户日常配置。
- 固定媒体应答与真实 offer 的顺序、MID、传输和负载不符，安全诊断差异标志为 30。
  原生应答改为按 offer 协商、保持本地凭据和证书，最终差异为 0。
- DTLS 握手实际完成（发 3 收 3）；修复计时器注册后也能在丢包测试中重传。
- 60 秒串流最终解码 2034 帧、音频收包 2462、渲染提交 3441，解码错误/丢弃均为 0。
  总运行 73 秒正常结束，未自动重排。没有用截图或鼠标证明显示效果。
- 最终交付构建另跑一轮：收到视频 2031、解码 2005、音频包 2438、渲染提交 3371，
  初期丢弃 26 后计数不再增长，解码错误 0，总运行 75 秒正常结束。

这证明真实服务的媒体已能进入 NRO 解码/渲染路径，不等于真机画面、声音和输入已经验收。
本轮服务器仍未启用手柄。后续记录应保留这种区别，不再把旧超时当作当前全部状态。

以下是 0.2.10 的历史基线：

0.2.10 + yuzu EA4176 + devkitA64 GDB 14.1 已真实运行：

- GDB 端口连接、NRO 启动指令读取、继续/暂停、main 断点。
- 用户提供的 vault/key 在 NRO 内恢复登录，LOGIN_RESTORED http=200；没有扫码。
- 自动普通排队（位置 1），14 秒时派发，15 秒时信令 HTTP 101，游戏授权成功。
- UDP 候选加入，RTC 处于检查状态；45 秒时 RTC_CONNECT_TIMEOUT。
- 音频、视频、解码、渲染计数均为 0。自动停止并保存结果，不宣称画面已显示。

最初在 main 之前的服务请求处等待。Borealis 的 Switch 初始化请求 ins:s，
但框架和应用没有使用 INS API。原生构建现在用自有初始化封装，省去这项未使用的
服务；网络、文件系统、字体、电源、NIFM 初始化顺序保留。改后实际命中 main。
GFN 构建仍使用原封装，未修改 vendored 文件。

一次恢复验证在 10 秒时网络失败，保留登录后再次诊断成功；这不是凭据失效证明。
模拟器本次没有到达 DTLS 握手，不能把它的 RTC 超时和真机“DTLS 发 1 收 0”混为一谈。

## 调试组成

- NRO：模拟器运行的 Switch 程序。
- 同一次构建的 ELF：电脑上的源码/函数符号；不能混用不同构建。
- GDB stub：yuzu 在本机 6543 端口提供的远程调试接口。
- GDB 脚本：自动继续，记录 main、登录验证、排队入口、curl 数字错误码。
- 安全监控：区分入口、登录、队列、信令、RTC、媒体和结束，并保留首个失败事件。

本机验证的 NRO .text 加载基址是 0x80000000；脚本按此加载 ELF。
其他模拟器/版本必须重新检查加载基址和端口，不能直接套用。
参考 [GDB 远程连接与符号要求](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Connecting.html)。
真机网络标准输出可参考 [libnx nxlink 接口](https://github.com/switchbrew/libnx/blob/master/nx/include/switch/runtime/nxlink.h)；
nxlink 日志与 GDB 断点不是同一功能。

## 本机私有运行环境

根目录：C:/Users/y/Documents/Codex/CloudGenshin-Debug（仓库之外）。
模拟 SD：runtime/sdmc；AppHomePath 映射为 runtime/sdmc/switch/SwitchNOW。
用户已授权的 cloudgenshin-login.vault / .key 只放在此运行目录；不打包、不输出内容。

yuzu 配置使用这个模拟 SD；开启 use_gdbstub，端口 6543。配置中的路径组为
Data%20Storage，显式非默认值需对应的反斜线 default 标志为 false。
使用独立配置，不改动用户平时使用的 qt-config.ini。yuzu 自身仍会写本地缓存/日志。

## 自动测试开关和边界

在 AppHomePath 下放 cloudgenshin-autotest.txt，内容必须精确为一行
CGNX_AUTOTEST_V1，并以 LF 换行。没有有效开关时，正常 NRO 不会自动排队。

- 等待登录最长 180 秒，普通排队最长 300 秒，串流观测最长 60 秒。
- 每个进程只自动进入一次普通队列，不自动重排、切换账号、买时长或切付费队列。
- 缺少/被拒绝的登录允许显示二维码等用户确认；不自动完成二维码、短信或验证码。
- 网络失败保留登录文件。取消、退出账号都停止当前自动排队意图。
- 出现已知失败，脚本发停止文件并给队列/会话清理时间；清理是尽力而为，非云端 ACK 保证。
- ReportDirectory 下创建 stop.request 可请求停止；NRO 读取模拟 SD 下的
  cloudgenshin-autotest.stop。下次运行前必须明确处理已有停止标记，不能无限重启。

## 构建和运行

按原 Switch 构建脚本设置 CLOUDGENSHIN_DEBUG_SYMBOLS=ON，保留配套 ELF。
NRO 不携带 ELF 的 DWARF 调试段。不要从编译成功推断可玩。

本机运行示例（已有 yuzu、固件、密钥与独立配置，不下载或提供这些材料）：

```powershell
./scripts/start-cloudgenshin-autotest.ps1 `
  -EmulatorPath 'H:/ALL/yuzu-windows-optimized-pgo-ea-4176/yuzu-cmd.exe' `
  -NroPath './build/switch/SwitchNOW.nro' `
  -ElfPath './build/switch/SwitchNOW.elf' `
  -GdbPath 'C:/devkitPro/devkitA64/bin/aarch64-none-elf-gdb.exe' `
  -ConfigPath 'C:/Users/y/Documents/Codex/CloudGenshin-Debug/yuzu-debug.ini' `
  -SdRoot 'C:/Users/y/Documents/Codex/CloudGenshin-Debug/runtime/sdmc' `
  -ReportDirectory 'C:/Users/y/Documents/Codex/CloudGenshin-Debug/reports/next-run'
```

若不开 GDB，独立 yuzu 配置也必须关闭 stub；否则 NRO 会停在入口等待调试器。
GDB 只连接成功并不代表已继续运行。当前这版 stub 不支持 detach，结束由测试脚本协调。

status.json 只有允许的事件和数字指标，未知行被丢弃；控制台只在阶段/指标变化时
输出摘要。标准输出和错误流被有界缓冲读取后丢弃，不保存原始协议/完整控制台。
已有 stop 标记、缺文件、超时、模拟器退出均需检查，不算成功。
明确失败或未完整结束会返回非零退出码；正常结束也只表示测试结束，不是游戏可玩。

## 测试

scripts/test-mihoyo-host.ps1 的 mihoyo_autotest_policy 检查 opt-in、登录后一次排队、
忙碌/取消、截止时间和不重复尝试。mihoyo_launch_flow、mihoyo_login_store、
mihoyo_ui_language 回归同样通过。tests/cloudgenshin_monitor_test.ps1 检查阶段解析、
首个失败保留、未知输入/越界数字抑制。真实帐号只用于独立运行环境，不进入测试夹具。
