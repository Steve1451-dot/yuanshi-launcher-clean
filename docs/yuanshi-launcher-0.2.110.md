# yuanshi launcher 0.2.110：名称、透明 RTT 与旧安装兼容

日期：2026-11-04。原项目工作目录、源码命名空间和兼容路径不迁移。

## 用户可见变化

- 窗口、首页、诊断标题、NRO 的 NACP 应用名和新安全日志抬头统一使用
  `yuanshi launcher`；分发名为 `yuanshi-launcher-0.2.110`。NACP 的 16 个
  语言槽均已补齐，俄语、韩语、繁体中文和简体中文不再显示空标题。
- 新简洁图标：炭黑底、米白抽象飞翼标记与大留白；统一 NRO、RomFS 与安装包图标。
  不再使用“元”字、三色环或上游 OpenNOW 的绿色云图标。
- 游戏标题及相关中英文说明在显示层使用 `ys`、`zmd`、`bt`。不改认证配置里的
  game_biz/app_id、端点、签名、设备身份、用户加密库或保存的游戏选择。
- 三个游戏串流右下角默认常驻 `15 ms` 一类延迟文字，没有背景板、交互热点或焦点。
  未建立有效连接或读不到值时是 `-- ms`，不会拿节点预测速、解码耗时或常数代替。
- README 改为本项目的中文介绍，说明非官方、仅供学习参考、安装、按键、当前限制
  与许可证归属，不套用其他远程桌面项目的功能和验证结论。显著说明 ys/zmd/bt
  多合一（三个入口共用一套启动器，但不代表三款均已完整可玩）。

## RTT 的数据和线程边界

使用 libpeer 已有 `peer_connection_get_rtt_ms`，优先平滑 ICE RTT，内部可能回退
SCTP 的 RTT 估计。这是当前连接的既有估计，不是另发探针的持续测速，也不是输入
到屏幕的总延迟；连接正常但底层尚未取得新测量时可能沿用平滑缓存。

只由网络 owner 每秒查询一次；未完成连接不读 peer RTT，UI 不访问 peer、不取
SCTP 锁。用一个原子快照携带时间和值，过期 5 秒或关闭会话后显示未知。值域有界，
零值作为合法的亚毫秒取整结果，负值/异常值不画成低延迟。未加逐帧/逐包日志、
后台测速线程、无上限队列、额外设备信息发送或超频。

两个 native 视图复用同一绘制方法，保留原软硬解管线、输入报文及浮球操作。

## 兼容与打包

SD 继续使用 `switch/SwitchNOW/SwitchNOW.nro`；原 `cloudgenshin-*`、`hkrpg-*`、
`endfield-*` 配置和 `backgrounds/genshin|starrail|endfield.*` 不改名。
加密库 AAD/格式不变，不重建身份，不导出登录。新下载包使用新名称，安装时按原
目录覆盖程序即可。历史冻结版本不覆盖。

安全日志解析和轮转同时接受旧 `CloudGenshin-NX native alpha` 与新
`yuanshi launcher native alpha` 抬头。原机器事件名不变，旧日志仍可诊断，旧日志
段仍受约 8 MiB 的原有总量限制；外来、损坏、符号链接文件不作为轮转对象。

打包保留 README、仓库 LICENSE 与固定安装目录；新版本默认读取 app_version.hpp，
已有同名包不自动覆盖。包中不含账号、APK、自动测试标记或设备信息同意标记。

## 本轮验证

12 项相关 C++ 主机测试通过：`network_latency`、`network_latency_draw`、
`mihoyo_safe_event_log`、`mihoyo_safe_log_archive`、`mihoyo_safe_log_writer`、
`mihoyo_rtc_session`、`mihoyo_ui_language`、`mihoyo_passport_token_resume`、
`mihoyo_sdk_bridge`、`controller_mapping_draw`、`compact_stream_menu_draw`、
`mihoyo_game_profile`。安全监控 PowerShell 测试通过，含新旧抬头读取。

新增覆盖：一秒采样节流、零/无效值、5 秒过期、时钟回退、原子交接、断开后未知，
两层文字无实心底框、右下角坐标和剪裁、两种语言简称、旧日志满环仍可轮转。
这些测试是合成数据，不代表远端游戏已入场，也不是本版实机视觉和性能验收。

Switch 构建、NACP 标题/版本和分发哈希以随包 `构建校验.md` 为准。

## bt 排障边界

本轮继续对照本地官方 combo-web 源码。`setComboLoginParamsFromCloudGame` 只在
`channel == qt` 时保留特定 normalized 参数；不能因函数存在就为当前 H5 路线
虚构 QT 数据。`_createComboLoginSuccResult` 的顶层/嵌套合并及设备/账号字段仍须
与当前已验证响应一致。

官方 SDK 的 `login` 会检查网页登录状态并调用 webLogin；当前客户端在派发前续期，
游戏内桥使用该次已验证授权。重新获取的时机是待验证的差异，不是已经证实的错误
根因。旧原生 Combo 刷新试验不等价于这条网页登录路线，未默认开启；本轮也没有
把 HTTP 放进媒体回调或反复开相同云实例。

0.2.109 已实测“官方设备指纹已取得且游戏已读取”，但仍显示 `1001_2`。本版未修复
该错误，不宣布 bt 可正常进游戏，不把 ys/zmd 的历史实机反馈升级为本版全场景保证。
没有重开已消费的单次设备信息发送同意开关。
