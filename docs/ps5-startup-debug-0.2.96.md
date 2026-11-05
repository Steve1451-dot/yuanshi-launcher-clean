# 0.2.96 PS5 启动调试候选（2026-10-31）

状态：生产 Switch 构建及离线回归通过；yuzu 已启动实际 NRO 并到达保存登录
验证，但电脑网络超时阻止继续。**不能宣称 PS5 在线画面、按键或体感已修好。**
没有部署或改写 USB 连接的真实 Switch，没有要求重新扫码，没有复制/输出登录库、
令牌、二维码、原始 SDP 或账号请求。没有修改系统代理、路由或用户日常模拟器。

## 实机旧版事实

从 `Switch / 1: SD Card / switch / SwitchNOW` 只读导入安全日志与少量非敏感
输入配置。存于工作区 `outputs/switch-ps5-debug-2026-10-31/read-001`。
本机 H 盘桌面的旧副本不是此次最新依据。

| 0.2.95 实际会话 | 安卓＋PS5（archive/stream-safe-07.log） | 随后的标准模式（08.log） |
| --- | --- | --- |
| 串流终端 / 虚拟手柄 | 3 / 054c:0ce6 | 10 / 045e:028e |
| 云端 PS 能力标记 | 1 | 0 |
| RTC | 已完成 | 已完成 |
| 控制 / SDK | GCC ready、SDK Init/login | SDK Init/login/will_enter_game |
| 视频 / 音频 | RTP、单元、解码、渲染、音频均 0 | 收到首个视频单元并解码 |
| 体感开启 / 发送 | 均 0 | 均 0 |
| 结束原因 | 用户停止，不是崩溃 | 用户停止 |

两轮不是严格同一 pod/网络条件的 A/B；旧开关同时改变了终端和手柄，不能据此
断言只由安卓终端或只由 PS5 引起。PS 能力标记也不是输入已被游戏采用的证明。
设备当前已切回 standard，终端覆盖 10、手柄 xbox360；全部保持原样。

## 修正与可验证改动

1. **设备信息不再等第一帧。** APK `libclient_core_impl.so` 的
   `ClientCoreImpl::onAuthPassed`，0x6db74–0x6db84，直接向授权信令发送命令
   20017；不是等待解码或 input-ready。旧实现的 `frame_geometry_ready_` 门槛
   在无画面会话永久挡住这条消息。现在授权后用客户端 1280×720 初始屏幕信息
   同步，实际视频几何改变后再更新；失败最短 500ms 重试，恢复重新同步。
   这是确切的顺序差异修复，**尚未证明是零媒体的根因或解决方案**。
2. 原神首页选择器保留标准、安卓＋PS5，增加**网页终端10＋PS5 对照**。
   后者只改变标准网页路径的虚拟手柄身份，以分离变量。每份 type-17 状态和
   设备列表都使用同一 VID/PID，仍是 APK XinputData，不伪造 DualSense HID。
   仅原神生效，其他游戏/本地菜单不变；标准模式仍尊重既有诊断覆盖文件。
3. 模式仍在 `AppHomePath()/genshin-input-mode.txt` 原子保存、备份恢复，默认标准。
   v1 新值 `web-ps5`，旧 `standard` / `android-ps5` 兼容。不开机强制开启实验，
   不改用户键位和本地体感辅助偏好；实验只在会话内暂停转摇杆以避免混合输入。
4. 首帧前每会话只作一次 20 秒非致命分类：没有识别音视频、仅音频、视频 RTP
   未组成单元、或视频单元未解码。正常等待上限仍为120秒，不自动重排或改身份。
   保存所选 H264/Opus payload 编号，绝不保存 SDP/地址/密钥。
5. 保存登录验证保留数字 CURLcode；不再只出现 `SAVED_LOGIN_RETRY_LATER code=0`。
   仍保留登录库，网络故障不变成凭据失效、不自动重新扫码。
6. yuzu EA4176 GUI 的 `-c` 不能覆盖全局配置。测试脚本现在要求专属 portable
   `user/config/qt-config.ini`，校验 `[Data%20Storage]` 的 SD 路径和非默认标记。
   预先拒绝错误路径，不把另一张 SD 的日志当成当前 NRO 没启动。

## 本次 yuzu 实测

专属环境：工作区 `.opennow-msys-tmp/yuzu-ps5-online/emulator`。
使用既有私有 Debug 模拟 SD 的授权登录库，没有将凭据导入测试夹具或输出目录。
后台命令行运行，没有操作 GUI、发送键鼠、OCR 或截图，也没有跑定时任务。

证据在 `outputs/ps5-yuzu-0.2.96/`：

- `android-ps5-run-1`：旧脚本实际跑在空的探针 SD，监测错误；不算客户端启动失败。
- `android-ps5-run-2`：正确 SD 启动；保存登录验证网络失败，尚未派发。
- `android-ps5-run-3`：改用确实在线的 WLAN，但仍同样超时；不是云端拒绝凭据。
- `android-ps5-run-4-gdb`：同一次 NRO/ELF 的安全断点命中 main、ResumeLogin，
  `GDB_HTTP_CURL_CODE code=28`。只取数字错误与调用位置，不读取秘密内存。
  没有排队、游戏授权、PS5 连接或视频成功证据。

电脑原生 curl 对官方云账号入口（含 IPv4 单独探测）和官网公共入口也连接超时。
本机 DNS 当时把官方域名解析到 `198.18.0.*`，默认路由中存在虚拟网络接口。
这是当前电脑到官方服务的连通问题证据，不能证明具体由哪个代理/路由造成，
更不能归因于 PS5 或真实 Switch 网络。未擅自更改全局网络。

测试进程均已结束，Debug 原选择 endfield 与原停止保护标记已恢复；临时实验设置、
按键探针已改名停用。真实 Switch 始终只读。

## 回归与构建

相关 C++ 主机用例通过：controller_mapping、controller_mapping_draw、
genshin_input_experiment、mihoyo_media_startup_policy、mihoyo_rtc_session、
mihoyo_sdp_answer、mihoyo_ui_language、mihoyo_gamer_login_http、mihoyo_launch_flow。
最后一项在新增“网络错误28仍保留登录、不扫码”的断言后再次执行。
监测脚本测试、专属 portable 配置/SD 错配拒绝测试通过。

Session 用例执行生产 Session 实现、人工传输：分别验证终端3与10的 PS5
StartGame、首帧前设备信息、列表、按键、合法云端反馈前禁止 IMU，反馈后本地
编码/发送计数。**这些不是在线接受或游戏瞄准证明。**

Switch 完整构建125步通过，后续15步和31步增量通过，版本0.2.96。
仅见既有第三方 stb 警告，无第三方库改动。相关差异空白检查通过。

在线调试用的首个构建冻结为 `outputs/ps5-yuzu-0.2.96/SwitchNOW-0.2.96.nro`
及匹配 ELF，SHA256 `E093504E02B6133E02DD6B538F333D9C3E3A2359E68D136F8EC819CE2E63EC8E`。
它早于最后一项数字网络诊断，保留原件以匹配 GDB 证据。

最终候选为 `outputs/ps5-yuzu-0.2.96/candidate-final/SwitchNOW.nro` 及匹配 ELF，
38,961,372 bytes；SHA256：
`AA9CCBF86268D0F99CEABF481FAAF755A94291B3C331CEDED21685440D8F4356`。
未打包安装版或覆盖实机，不称为已通过 PS5 测试的交付版本。

## 后续验收

先恢复电脑到官方 HTTPS 服务的连通性；如需改系统代理/路由应先取得用户授权，
不绕过风控、TLS 或登录验证。网络恢复后，用最终匹配 NRO/ELF 在 yuzu 先测
安卓＋PS5（验证早期设备信息确实发送），再按需测网页＋PS5 对照。
每轮只普通排队一次，不付费、不自动重复排队。
必须区分：实际视频包/解码/渲染、实际游戏按键响应、云端 IMU 开启、IMU 本地发送、
最后的游戏瞄准反应。网络、首帧和输入未过时，不让用户反复装包验收体感。
即使 yuzu 全部通过，也仍需真实 Joy-Con 的坐标方向、握持和瞄准手感验收。
