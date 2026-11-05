# 0.2.45：网页登录独立入口与 SDK 编号类型修正

> 历史版本记录。35-4301 的最终根因是旧 vault 缺 `account_type`，0.2.46 已修复
> 并实机进入游戏验证；见 `docs/fix-35-4301-2026-10-21.md`。网页登录仍保留为
> 独立入口，未改变现有扫码/恢复登录路径。

本版本承接 0.2.44 的字段保留修复，不把交接中的“下一步”当作已经完成。
它仍是验证版本，不是已经确认能进入原神的成品。

## 确定性修复

- SDK `login_login` 将 `app_id`、`channel_id` 作为 JSON 整数回传，
  与官方 Android `UserData.toDataJSON()` / 网页 Combo 结果一致。
  空值、尾随字符、负数和超出 int32 的编号不再伪装成成功。
- 保留 0.2.44 的附加字段类型、旧 vault 兼容、有限保活和安全诊断。
- 调试器 HTTP 返回码断点随 HTTP 源文件移动，避免在返回码尚未赋值时读取。
- 官方二维码地址可带移动页面 fragment 路由；票据只从 query 比较，
  不因合法 fragment 误拒二维码，也不把 fragment 当作 query 的一部分。

## 新的独立入口

原神主页增加“网页登录 / Web sign-in”。原扫码、恢复登录、其它游戏的路径不替换。
目前只为已核对的 `hk4e_cn` 配置开启，不能宣称已通用适配三款米哈游游戏。

流程：

1. 官方 `createQRLogin` 创建二维码；同一设备标识下每 2 秒 query，最多 3 分钟。
2. 手机确认后，同一个内存 Cookie 会话调用 `webVerifyForGame`。
3. 只接受明确标记为 `token_type=1` 的令牌，不把 Panda 的 Account token 猜成 stoken。
4. 官方 `combo/granter/login/webLogin` 返回游戏登录数据，账号须与上一步一致。
5. 云账号验证成功后才替换内存登录并原子写入已有加密 vault。

取消、失败、账号不匹配均不清空或覆盖旧登录。Cookie 仅在这次登录 worker 的
内存中共享；不读取浏览器 Cookie、不创建 Cookie 文件、不降低 TLS 校验。
设备型号报告 Nintendo Switch / Horizon，不伪造浏览器指纹；没有官方指纹时省略。
实名、安全验证要求交给用户处理，本机不假确认或绕过。

网页账户 SDK 公共头版本为 2.57.0；游戏 Combo SDK 的 `x-rpc-mdk_version`
为其官方 bundle 中的 2.55.0，两者不是云串流 SDK 的版本。
设备 UUID 在一次扫码中保持一致，并随成功 Authorization 保存；新一轮扫码
生成新 UUID，不修改旧登录的设备标识。

## 验证与限制

- `mihoyo_passport_login`：官方响应形状的合成样本、顺序、取消、账号不匹配、
  越界返回码、二维码来源/票据校验；不会访问账号或把凭据打进诊断。
- `http_cookie_session`：链接实际 HTTP 实现，验证内存 Cookie 为显式 opt-in、
  普通请求不继承、不会设置 COOKIEJAR、TLS 开关保持开启、配置失败即终止。
- SDK 桥测试检查回包解密后的实际 JSON 类型及编号异常；其余相关回归另行运行。

这些主机测试只证明实现行为，不能证明服务端会给出何种 Confirmed 数据。
Confirmed 响应是否通过 Set-Cookie 建立完整网页登录态、缺少官方指纹是否被接受，
必须实际扫码验证。若失败，安全日志只记录固定事件、HTTP 状态、retcode；
不导出 token、Cookie、票据、二维码内容。不会为了“成功”硬填未知凭据结构。

调试专用 `cloudgenshin-autotest.web-login` 精确内容 `CGNX_PASSPORT_LOGIN_V1` 加换行，
启动时仅测试网页登录；不会自动排队。正式包不包含任何 autotest 标记或登录文件。

`35-4301` 是否消失、启动界面是否出现加载进度，仍要以扫码后云会话画面和
`GAME_SDK_WILL_ENTER_GAME` / `GAME_SDK_ENTER_GAME` 等事件共同验证。
不将视频收到/解码/提交渲染或 `login_login ret=0` 直接当作进入游戏证明。

## 2026-10-21 本轮实际验证

- 15 组相关 host 测试、PowerShell 安全日志监控测试、完整 devkitA64 NRO 构建通过。
- yuzu：第一次新网页登录 create 收到 HTTP 200，但因官方 URL 的 fragment
  被本机误拒；修复后成功进入 `QR_WAITING http=200`。按用户要求停止扫码测试。
- 普通队列第一次遇到 5/5 测速失败，增加错误码与失败分类；未改变测速预算、
  不伪造 RTT。后续复测 5/5 成功，因此没有把临时失败归咎于账号。
- 核对发现原模拟器 key 与用户桌面提供的 key 不匹配；停止模拟器后，将旧对
  在调试目录私下备份，成对导入桌面提供的文件，未读取或输出凭据正文。
- 桌面存档 `LOGIN_RESTORED http=200`，无需扫码即可自动普通排队、派发、
  收到音视频、响应游戏 SDK 请求，并自动点击底部“点击进入”。
- 检查本次生成的灰度视频帧，仍明确复现 `35-4301`。当前不是可游玩成品。
  因此数字类型修复不能被称作该错误的完整解决方案。
- 桌面存档本次的 channel_token/metadata/account_type/combo_id 存在标记均为 0。
  这是观察结果，不足以证明错误根因；不通过猜值或要求用户反复扫码掩盖。
