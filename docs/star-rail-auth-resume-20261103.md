# 崩铁登录链路复核（2026-11-03，0.2.107 内部候选）

前半部分记录 0.2.107 的历史实验；0.2.108 的正式接线与新扫码结果见文末更新。

## 已确认与未确认

冻结发布目录 `outputs/three-game-0.2.107` 没有被覆盖。本轮构建仍使用 0.2.107
内部版本号，各候选二进制不同；联机结果必须结合报告里的 NRO SHA-256 使用。
尚未证明进入崩铁游戏，不能把标题视频、正常解码或 SDK login 回包当作完成。
本轮没有真实 Switch 验收，也没有修改原神、终末地的媒体或手柄管线。

## 本轮联机证据

所有报告在工作区根目录 `outputs/star-rail-online-20261103`，不是仓库子目录。

| 记录 | 实际结果 | 结论 |
|---|---|---|
| http-phases-107 | NRO 恢复保存登录后开始测速；yuzu 本体以 0xC0000005 退出，未派发 | 不是已证明的游戏内错误或 NRO 崩溃 |
| http-phases-accurate-107 | 隔离 yuzu 改为准确 CPU 模式；11/11 测速成功、派发、RTC、SDK 和画面可达；175 秒、2488 解码帧、解码错误 0、8003 音频包；标题点击后仍出现 1001_2 | 前两轮节点/派发超时本轮未复现，但不能宣称修复；游戏登录错误仍存在 |
| official-web-resume-107 | 官方 getTokenBySToken 返回 HTTP 200、retcode -5300；止于阶段 1 | 不重试、不自动换接口、不分配游戏；实验授权没有替换原保存登录 |
| official-game-web-resume-107（20261104 目录） | 显式 GameToken 路线同样 HTTP 200、retcode -5300，固定消息提示位 1 | 旧凭据无法凭此恢复新的网页授权；没有改写原登录或尝试绕过验证 |

yuzu 日志还含未实现的套接字选项断言。这类 stub 日志与本体访问异常同时存在，
但不能据此认定它就是崩溃原因。准确 CPU 模式跑通一次也不代表真实 Switch
性能被模拟或模拟器稳定性已经验收。

## 请求诊断

HttpRequestError 在句柄释放前取回固定数字：DNS、TCP、TLS、传输就绪、首字节、
总时间和上传正文大小。派发准备和初次派发失败保留这些计数；不输出异常正文、
URL 路径、HTTP 头或凭据。不加初次派发重试，也不放宽 TLS。

这些累计时间只描述 libcurl 所观察到的阶段；就绪不等于服务器已处理，上传计数
也不是业务成功。未捕获的数据不伪造成有效耗时。

## 官方账号到网页登录的独立实验

新增 `mihoyo/passport_token_resume.*`，正常入口默认关闭，只接受独立的崩铁
自动测试标记。流程最多四个固定官方请求：

1. 通过官方账号接口验证旧凭据，并取得服务签发的 stoken 和 mid。
2. 官方 exchange 交换 token_type=4。
3. webVerifyForGame 验证网页账号并取得明确 token_type=1 的凭据。
4. 崩铁 Combo webLogin 取得真正的网页登录结果。

每步验证账号一致性和必要字段。Cookie 仅在内存组装，值必须由官方服务签发，
只发给固定 HTTPS 官方主机，不读取浏览器、不写 Cookie 文件、不跟随跳转。
风险、验证、HTTP/网络异常、账号不一致或畸形响应均停止；没有验证码绕过。

只有完整的新授权再通过云服务验证，才允许原子保存到现有加密 vault。失败保留
原授权，登录库格式不变。新路径尚未正式启用，不能称为已修复游戏登录。

旧 Combo 二维码返回的 `Account.token` 没有明确类型证明，原代码把它一律注释
为 stoken 并不严谨。SToken 与 GameToken 是两条独立、显式选择的诊断路线；
不依据字符串猜测，不在接口拒绝后自动轮换尝试。GameToken 路线的结果也为阶段 1
拒绝；提示位 1 仅说明消息包含过期/重新登录词语，不证明旧 token 的真实类型。

## 协议依据

以下均为工作区已有官方公开产物或用户提供 APK 的静态解析，不是账号文件：

- `reference/android/starrail-cloud-4.6.0/assets/server_env_config.json`：生产 app_id=8、passport_app_id。
- 崩铁 `LoginManager.java`：loginBySToken、loginByGameToken、getCookieTokenBySToken。
- 崩铁 `RequestUtils.java`：SDK 2.54.0 请求头、生产 DS 计算及参数序列化。
- 同一 Passport SDK 的原神 `LoginApiService.java`：两个登录升级接口均 POST，SToken 版无请求体；exchange 路径。
- 崩铁 `GetTokenByOldTokenEntity.java`、`TokenExchangeEntity.java`、`TokenType.java`：token_type 与服务签发的账号字段。
- `reference/web/sr/combo-web.7d4a7ffa.js`：Combo webLogin 与游戏 SDK 回包来源。

本轮不从 -5300 推断出“账号失效”“风控”或“必然是 GameToken”，这些需要独立
证据。拒绝消息只允许输出固定短语的整数提示位，不输出服务器消息原文。

## 验证与收尾

新增测试覆盖两种显式来源的签名/请求契约、同账号校验、类型错误、注入字符、
大小边界、风险/验证、HTTP/网络异常、取消和原对象不被修改。测试只用合成数据。
自动测试标记需同时满足普通开关、崩铁网页登录开关；GameToken 还需第三个开关。
监控保留阶段、错误、来源和候选 NRO 哈希，不把官方拒绝当作测试成功。

本轮结束后需关闭所拥有的 yuzu、归档本轮测试标记，恢复隔离配置原来的 SD
路径和 CPU 设置；不得删除用户登录、设置或旧发布包。

## 0.2.108 更新（2026-11-04）

定位到确定性配置错误：Passport 二维码请求头原先复用原神的 app_id。
官方崩铁 `reference/web/sr/web.4f0790db.js` 配置及 SDK 初始化明确使用
`c90mr1bwo2rk`，不是原神 `c76ync6mutq8`；已按游戏区分，并保留原神请求不变。
这是 Passport 应用标识，不是 Combo app_id=8，也不是云客户端类型 19。

崩铁主入口现在使用官方网页登录。旧 vault 兼容读取；缺少官方签发 MID 的旧
授权不会被删掉，显示“更新网页登录”。只有新网页登录、同账号验证及云服务验证
都成功后，才原子保存 SToken 与 MID 到原有 AES-GCM 加密库。失败保留旧登录。
MID 是可选的加密字段，不出现在普通配置或日志中。

保存了明确 SToken/MID 的新登录走正式续期路径，直接从阶段 2 的 exchange 开始，
不再对已验证凭据运行旧来源猜测/升级。SToken 与 GameToken 的旧授权实验仍需
独立诊断开关，拒绝后不自动换接口。Genshin/Endfield 不启用这一崩铁策略。

本轮报告在根目录 `outputs/star-rail-online-20261104`：

- `legacy-guard-108`：旧云登录可恢复，缺少网页上下文时在派发前停止，正确报告
  `NeedsOfficialWebSignIn`，没有浪费一次游戏分配。
- `passport-login-108`：用户扫码并手机确认后，依次收到
  `PASSPORT_QR_CONFIRMED`、`PASSPORT_STOKEN_VERIFIED`、`LOGIN_OK`、
  `LOGIN_SAVED`。真实网页登录和加密保存已通过，不是仅显示二维码。
  该运行是登录专用，最终监控超时退出不代表登录失败，也不代表进游戏。
- `web-session-game-108`：重启后恢复登录；官方网页续期到阶段 4 成功，保存成功。
  随后 11 个测速目标全部连接超时（curl=28），在派发前停止；本轮没有进入队列、
  RTC 或取得新画面，旧帧转储时间不能当作新结果。

该结果证明新授权可跨进程保存和续期。最终诊断候选
`ping-phases-web-session-108`（NRO ADA0E760…）随后 11/11 测速成功，派发、RTC、
SDK 登录与开场动画可达；73 秒遥测 code=1001，帧转储明确显示 1001_2。101 秒
主动停止时 1306 解码帧、解码错误 0、4138 音频包，未收到 SDK 入场信号。
因此新网页登录确已修复保存/续期链，但不足以修复游戏内错误。
补充的测速数字诊断区分 DNS、TCP、TLS、传输就绪、端口、系统错误和耗时；
不导出测速地址或响应，不延长/取消 TLS 验证，不添加派发重放或付费队列。
