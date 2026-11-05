# 0.2.75：实机“选择服务器后回已登录”的确定缺陷与剩余证据

用户反馈实机没有进入排队/串流，状态在选择服务器后回到已登录，并指出终末地
比其他游戏多一个刷新二维码按钮。本轮暂停崩铁分析，优先处理该反馈。

## 已确认的缺陷

`endfield_start_worker` 在派发后刷新 game CLIENT grant。旧代码将
GAME_AUTH_FAILED / VERIFICATION_REQUIRED / AGREEMENT_REQUIRED 以
`update_status=false` 发布，然后在授权不可用时 return。首页仍保留先前的
节点阶段；active 变 false、vault 仍 Ready 后，StatusCard 回退为 Signed in。
这确定会隐藏一次启动失败，但不能在缺少实机当次日志时断言服务器返回了 109
或其他某个具体数字码。

## 修改

- `endfield/launcher_status.hpp` 为授权结果和首页提示提供纯策略。空授权码即使
  status=0 也显示失败并归为 -21；109/-23 保留为需用户操作，不假装登录失效。
- 关键失败/需确认状态即使收到 log-only 请求，也必须更新 UI 和清空旧排队状态。
  普通形状诊断和队列清理仍可仅写日志，不覆盖失败原因。
- State 保存当前阶段的数字码与 HTTP 状态，首页在具体提示前显示这些数字。
- 派发、派发后授权刷新各有固定安全阶段事件。账号凭证与协议请求均未修改。
- 删除独立 `endfield_refresh_` 控件及焦点引用；未登录通过主登录按钮获取新码，
  登录后保留开始/退出登录。二维码卡只说明凭证保存，不当作游戏连接证明。
- CMake 已从 app_version.hpp 派生 CloudGenshin 标签，build/version 验证以及
  NACP 元数据随之为 0.2.75。OpenNOW/其 forwarder 的独立 0.0.6 标签未改动；
  本测试包由明确的 CloudGenshin 0.2.75 路径打包，不使用旧 OpenNOW 包名。

## 检查

8 项相关主机测试通过：endfield_launcher_status、mihoyo_ui_language、
endfield_saved_game_authorization、endfield_post_frame_policy、endfield_game_bridge、
endfield_account_http、mihoyo_launcher_input_policy、mihoyo_game_profile。
新增回归覆盖 HTTP 200 拒绝、空授权码、设备确认/协议、log-only 吞错误、
清理不得覆盖原因。安全监测和 6 项 DTLS/UDP ctest 通过，限定文件 diff check 通过。
完整 NRO 构建及最后增量复核均通过。NRO 内新增提示和阶段常量已核对。

冻结测试包：`outputs/CloudGenshin-NX-0.2.75-endfield-launch-status-alpha.zip`。
NRO 40917852 字节，SHA-256：

`CCFDABDA52295ABAA65785B2840424602E57FB23A3426CC1E5F1CC6F4648B048`

## 尚未确定

桌面用户先前提供的 safe.log 时间为 2026-10-23，并非当次失败，不能用它推断
本次实机原因。没有当前 Switch 实机或新鲜错误码/日志可供检查。本轮未线上
启动另一个同账号会话，避免影响用户实机尝试；不能说“实机排队已修好”。
下一项最小证据是新版本首页数字码或本次 safe.log，勿索取 key/vault。

## 暂停的崩铁 APK 工作

用户提供 `C:/Users/y/Downloads/Programs/StarRailCloud_4.6.0.apk`，83084305 字节，
SHA-256 `8FF6C931E8CD91C137821F4E96C9AE333402E5AD6F890616265B0E966B80B4DA`。
尚未完成反编译或验证崩铁修复，不声称已分析完 APK。只结束本轮匹配该 APK
任务的 Java 进程，原文件保留。JADX all.jar 的默认入口是 GUI；恢复时须使用
本地 jadx.bat 或 `java -cp <jar> jadx.cli.JadxCLI` 的明确 CLI 入口，不能继续
使用 `java -jar` 误启动 GUI。本轮未留下后台模拟器或排队任务。
