# 联网调试改点地图（CloudGenshin-NX native）

> 目的：以后联网实测要改哪里、改完跑什么，一张表看完。
> 本文件只列路径与命令，不包含任何 token/vault/key 内容，也不得写入。
> 版本 0.2.51。仓库结构见 `AGENTS.md`；协议依据见
> `docs/cloudgenshin-handoff-2026-10-21.md` 与
> `docs/cloudgenshin-integration-status.md`。

## 常用命令

- 构建 NRO（结果 `build/switch/SwitchNOW.nro`）：
  `C:\devkitPro\msys2\usr\bin\bash.exe -lc "cd /c/Users/y/Documents/Codex/2026-09-22/da-ji/work/CloudGenshin-NX-native && bash scripts/build-switch-msys2.sh"`
- 全量 host 测试：`.\scripts\test-mihoyo-host.ps1`
- 聚焦测试：`.\scripts\test-mihoyo-host.ps1 -TestNames mihoyo_rtc_session,mihoyo_launch_flow`
- 测试清单在 `scripts/test-mihoyo-host.ps1` 的 `$tests` map；新增策略头后
  加一条 `名字=@(依赖源文件)`，用例放 `tests/名字_test.cpp`。
- 版本号：`app/src/app_version.hpp`（`CLOUDGENSHIN_APP_VERSION`），
  构建脚本与 CMake 自动读取。

## 联网时最可能要改的地方

| 想改什么 | 去哪里 | 对应测试 |
| --- | --- | --- |
| 每款游戏的身份字段（game_biz/app_id/biz_key 等）、终端类型 | `app/src/mihoyo/game_profile.cpp` | `mihoyo_game_profile` |
| 扫码→Combo→gamer 登录→派发 全流程顺序与字段 | `app/src/mihoyo/launch_flow.cpp`、`qr_login.cpp`、`combo_exchange.cpp`、`gamer_login.cpp`、`initial_dispatch.cpp` | `mihoyo_launch_flow`、`mihoyo_qr_login`、`mihoyo_passport_login` |
| 排队/派发/启动参数 | `app/src/mihoyo/queue_ticket.cpp`、`dispatch_prepare.cpp`、`dispatch_response.cpp`、`sdk_start_game_params.cpp` | `mihoyo_queue_ticket`、`mihoyo_dispatch_prepare`、`mihoyo_dispatch_response` |
| 节点测速与 ping | `app/src/mihoyo/ping_probe.cpp`、`ping_probe_network.cpp` | `mihoyo_ping_network` |
| RTC 信令/SDP/ICE/DTLS-SCTP、超时（15 s 关联、15 s DCEP 等）、输入发送、同传输重协商（媒体更新；ICE 重启仍拒绝） | `app/src/mihoyo/rtc_session.cpp`、`rtc_signal.cpp`、`sdp_answer.cpp`、`rtc_route.cpp`、`ice_candidate_policy.hpp` | `mihoyo_rtc_session`、`mihoyo_rtc_route`、`mihoyo_sdp_answer` |
| 独立控制通道（WebSocket 控制面）选择与回退 | `app/src/mihoyo/control_channel_policy.hpp`、`web_control_channel.cpp`；发送路径 `Session::send_channel_message` | `mihoyo_control_channel_policy`、`mihoyo_web_control_channel` |
| 输入映射（手柄/键盘/鼠标/触摸编码；手柄身份为 Xbox 360 0x045e/0x028e，仅在服务器启用虚拟手柄时发送 type-17 帧；触摸仅由覆盖层消费） | `app/src/mihoyo/input_packet.cpp`、`input_packet.hpp`（DataType 表）；触摸策略 `touch_forward_policy.hpp` | `mihoyo_input_packet`、`mihoyo_touch_forward` |
| 串流画面/输入/覆盖层行为（官方手柄门控、手柄 A 或点击继续、自动测试手柄 A 探测） | `app/src/cloudgenshin_stream_view.cpp/.hpp`、`cloudgenshin_stream_input.cpp`、`cloudgenshin_stream_overlay.cpp`、`mihoyo/stream_*_policy.hpp` | `mihoyo_stream_input_policy`、`mihoyo_stream_overlay_policy`、`mihoyo_stream_startup_policy` |
| SDK 桥接（GAME_SDK_* 事件、keep-alive、网页控制） | `app/src/mihoyo/sdk_bridge.cpp`、`sdk_report_diagnostics.hpp` | `mihoyo_sdk_bridge`、`mihoyo_sdk_report_diagnostics` |
| 登录存档（加密 SD 存档） | `app/src/mihoyo/login_store.cpp` | `mihoyo_login_store` |
| UI 文案与事件中文名 | `app/src/mihoyo/ui_language.hpp`（`Text()` 与事件翻译表） | `mihoyo_ui_language` |
| 诊断事件/计数 | `mihoyo/stream_diagnostics_policy.hpp`、`cloudgenshin_stream_view.cpp` 的 `snapshot_counters` | `mihoyo_stream_diagnostics_policy`、`mihoyo_safe_event_log` |
| 自动测试入口（仅 marker 触发） | `cloudgenshin_stream_view.cpp` 的 entry 序列、`cloudgenshin_autotest_policy.hpp` | `mihoyo_autotest_policy` |

## 联网实测前必须保持的硬约束

- 官方协议路径；TLS/证书/指纹校验不关不弱（证书在 `build/switch/romfs/certs/`）。
- 不打印/打开/存储 token、vault、key；诊断只记字段存在性与状态码。
- 不自动购买、不切付费队列、不绕验证码/实名。
- 默认最低画质 720p、`kBitRateLow 4000`、FPS 30；登录/扫码/排队/登出不回归。
- 默认永不自动输入；只有 SD 上 `cloudgenshin-autotest.click`（`CGNX_ENTRY_CLICK_V1`）存在时允许最多四次进入确认（服务器启用虚拟手柄时用手柄 A，否则用官方鼠标点击）。
- 实机验证后台运行 yuzu 收日志；禁止截图/抢焦点；弹窗 35-4301 后不发 Esc。
- 只看本地提交，不 push；禁止 `git clean`/`reset --hard`。

## 已知未完成项（产品级）

- 服务端发起的 ICE 重启式重协商（同传输媒体更新已支持）、自动重连、
  会话中途信令端点切换、凭据刷新。
- 计费/协议/用户中心界面；完整网络基准测试（丢包/抖动）。
- 扩展游戏消息（文字输入等）。
- hkrpg/nap 实机扫码确认（用户不愿操作）；nap `-100002` 为服务端白名单门槛。
- 触摸转发在 0.2.51 已从实机路径移除（避免 pod 切回触摸 UI）；仅保留
  `touch_forward_policy` 与 host 用例。
- 手柄身份已改为 Xbox 360（0x045e/0x028e），但 `CaGameConfig` 仍是官方门控：
  两端开关都为 0 时 pod 会丢弃 type-17 帧，这是当前"进去是键鼠"的根因，
  需要服务器侧开关打开或换号验证。
