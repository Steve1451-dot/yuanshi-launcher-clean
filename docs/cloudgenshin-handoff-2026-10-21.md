# CloudGenshin 移植进度交接（2026-10-21）

> 目的：在换机器/换会话前，把当前可运行状态、已确认协议、未提交改动、
> 35-4301 排查证据和下一步完整保存下来。
> **本文件不包含任何 token/vault/key 内容，也不得把 vault/key 内容贴进来。**
>
> 仓库：`C:\Users\y\Documents\Codex\2026-09-22\da-ji\work\CloudGenshin-NX-native`
> 版本：`CLOUDGENSHIN_APP_VERSION "0.2.46"`
> 状态：**0.2.46 已修复 35-4301，并在 yuzu 实机进入真实游戏场景**（角色/地图/HUD）。
> 原神目标完成；下一步依次验证 hkrpg、nap（各需用户扫码一次）。
> Git：基线 `1aef982`；0.2.46 已验证并做本地提交（不 push）。
> 禁止 `git clean/reset --hard`。

---

## 1. 目标与硬约束

目标：完整复刻官方云原神 Switch 客户端的输入/SDK 桥接与协议流程，先让原神进入游戏，
再用同一套方法完成星穹铁道（hkrpg）和绝区零（nap）。

硬约束（不要放宽）：

- 走官方浏览器/网页协议路径，不用旁路；TLS/证书/指纹校验不得关闭或削弱。
- “完整复刻”官方 web/Android 客户端行为，协议字段、顺序、流程以官方实现为准，不自创。
- 绝不打印/打开 tokens、vault、key；诊断只允许“字段是否存在”这种 presence-only 事件。
- 不自动购买、不自动切队列、不绕验证码；客户端只模拟手柄/键鼠。
- 默认最低画质 720p、`kBitRateLow 4000`、FPS 30；UI 极简双语。
- 登录/扫码/排队/登出不得回归。
- 默认永不自动点击；只有 SD 上存在 `cloudgenshin-autotest.click`（内容
  `CGNX_ENTRY_CLICK_V1`）时才允许一次性进入探测/点击序列。
- 全部改动不提交、不 push。
- 实机验证用 yuzu 纯后台运行 + 日志收集；禁止截图/抢焦点
  （PrintWindow/Ctrl+P/SendKeys 都不行）。

---

## 2. 历史实机状态（0.2.43，修复前）

以下 0.2.43 证据只用于说明修复前的 35-4301 复现，不是当前状态。

最近一次运行：`gui-0.2.43-cps-1`。

- 运行日志在 SD：`C:\Users\y\Documents\Codex\CloudGenshin-Debug\runtime\sdmc\switch\SwitchNOW`
- 已把日志与 12 张 `frame_dump_*.bmp` 复制到报告目录：
  `C:\Users\y\Documents\Codex\CloudGenshin-Debug\reports\gui-0.2.43-cps-1`
  （该目录原来为空；复制后 16 个文件，约 1.67 MB）
- `cloudgenshin-safe.log` 头部：`CloudGenshin-NX native alpha 0.2.43 / safe events only`
- 时间线（safe log）：
  - t=0：`LOGIN_RESTORED` / `LOGIN_SAVED`（使用的 vault 是 hk4e 的旧登录）
  - t=1：排队成功，`QUEUED_POSITION code=1`
  - t=12：`STREAM_ENTRY`、`STREAM_TERMINAL_TYPE code=5`、`STREAM_STARTUP_QUALITY code=2`
  - t=12：信令 101，RTC 建立，视频/音频开始
  - sdk-bridge `+55.2s`：发送 (0.5, 0.947) 的 move + 左键按下/抬起
  - 点击后约 2 秒：标题页变成“系统提示 / 错误码: 35-4301 / 确认”弹窗
  - t=85 → t=180：luma 保持 195（弹窗静止）
  - t=182：用户停止串流；`STREAM_STOPPED_BY_USER`、
    `AUTOTEST_FINISHED_NOT_GAMEPLAY_PROOF`
- 解码统计：4838 个视频单元、2 个 IDR、0 解码错误；8079 个音频包。
- 帧转储 6–11 全部相同，确认弹窗文字为：
  **“系统提示 / 错误码: 35-4301 / 确认”**。
  放大图在 `C:\Users\y\AppData\Local\Temp\opencode\cgframes\frame_dump_6_3x.png` 等。
- CPS/UAPC 已修复并实测发出：CPS 回复 74B、UAPC 65B（旧为 67B/67B）。
  **错误不变，所以 CPS/UAPC 不是主因。**
- 点击已到达游戏（2 秒后出弹窗），输入/坐标上链正确：
  `RtcMouse.cursorX/cursorY` 为 [0,1] 归一化，SDK 直接透传。
- 0.2.43 safe log 没有 `AUTOTEST_ENTRY_*` 事件，但 sdk-bridge 明确有 (0.5,0.947)
  的 move+btn；下一轮需要确认进入探测日志是否被门控/漏打（不影响点击本身）。

---

## 2.1 0.2.46 修复后实机状态（原神已进入游戏）

- 报告目录：`C:\Users\y\Documents\Codex\CloudGenshin-Debug\reports\gui-0.2.46-account-type-2`
- 同一 264 B 旧 vault，未扫码；未修改存档。
- t=31 s：`GAME_SDK_LOGIN_METADATA_PRESENT=0`、
  `GAME_SDK_LOGIN_ACCOUNT_TYPE_PRESENT=1`、
  `GAME_SDK_LOGIN_LEGACY_ACCOUNT_TYPE_RESOLVED=1`。
- t=68 s：一次性进入点击（marker 已删除）。
- t=71 s：`GAME_SDK_WILL_ENTER_GAME`。
- 无 `GAME_SDK_REPORT_LOGIN_35_4301`，无 35-4301 弹窗。
- `frame_dump_7.png` 至 `frame_dump_11.png` 可见角色、地图、小地图、队伍栏和
  游戏 HUD；完整时间线见 `events.log` 与 `docs/fix-35-4301-2026-10-21.md`。
- 第一次运行在同一构建上遇到 `DISPATCH_NETWORK_ERROR`（排队网络失败），重试
  后正常；与 35-4301 无关。

## 3. 35-4301 根因分析（已结案）

结案结论：旧 vault 缺少 `account_type`；正式修复只对已验证的
`hk4e/app 4/channel 1/open_id==account_id` 补官方米哈游账号类型 `1`，
不覆盖已有值，不外推到 hkrpg/nap。完整证据见
`docs/fix-35-4301-2026-10-21.md`。

以下保留排错过程，便于复查。

公开资料检索不到 `35-4301` 的明确解释；`4301` 属于账号认证失败家族
（如 12-4301 登录过期、32-4301 等）。当前证据链指向 **登录数据不完整/登录态来源不对**，
而不是输入或 CPS。

### 3.1 官方 login 结果形状（已确认）

官方 `combo-web` 中：

```js
function _createComboLoginSuccResult(e) {
  var t = e.data,
      n = Yt(e, oo),
      r = JSON.parse(t);
  return new Vn({
    device_id: ce.get("device_id"),
    app_id: de.get("app_id"),
    channel_id: de.get("channel_id"),
    channel_token: ce.get("stoken"),
    ...n,
    ...r
  });
}
```

- `Vn extends at`，`at` 是 `{ret, msg, data}` 的 Result 类，所以 `login_login`
  必须返回 `{ret:0,msg:"成功",data:{...}}`。**当前桥接的包装是正确的。**
- `channel_token` 来自 `ce.get("stoken")`，即 **passport 登录态**。
- `...n` 是 combo 登录结果里除 `data`/`heartbeat`/`fatigue_remind` 之外的字段；
  `...r` 是 `JSON.parse(e.data)` 展开的用户数据。
- 官方 `_invokeComboLogin` 先调 `Wn()`：

```js
Wn = function() {
  return tt(de.getUrl("comboWebLogin"),
            {app_id: de.get("app_id"), channel_id: de.get("channel_id")},
            {errorToast: false});
}
```

`comboWebLogin` =
`https://hk4e-sdk.mihoyo.com/hk4e_cn/combo/granter/login/webLogin`，
只发 `{app_id, channel_id}`，认证靠 **passport cookie**（stoken）。
没有 passport 会话时返回 `{"retcode":-109,"message":"请求失败，请稍后再试"}`。

### 3.2 安卓官方 `login_login` 字段（`UserData.toDataJSON()`）

官方安卓 `com/combosdk/module/platform/data/UserData.java` 返回：

- `app_id`（int）
- `channel_id`（int）
- `combo_id`（long）
- `open_id`（string）
- `combo_token`（string）
- `device_id`（string）
- `heartbeat`（bool）
- `account_type`（int）
- `asterisk_name`（string）
- 然后把 `this.data`（JSON 字符串）里的所有 key 展开到同一层。

当前 `Bridge::DispatchInvoke` 的 `login_login`（`sdk_bridge.cpp:594`）只返回：

```
device_id, app_id, channel_id, channel_token, open_id, combo_token
```

缺少：`combo_id`、`account_type`、`heartbeat`、`asterisk_name`、`data` 展开字段。

### 3.3 两条登录路径的差异（关键）

1. **Panda Combo QR（当前使用）**
   - `/combo/panda/qrcode/fetch` → `/combo/panda/qrcode/query`
   - 手机官方 App 确认后返回 Combo 载荷（`UserData` 形状）
   - **没有 passport `stoken`**，因此 `channel_token` 为空。
   - 当前 264B vault 来自这条路径。

2. **Passport 网页登录（官方 web/PC 使用）**
   - 浏览器先登录 passport（密码/短信/扫码）→ 得到 `stoken`
   - 再 `comboWebLogin`（带 cookie）→ 得到完整 combo 登录结果
   - `channel_token = stoken`
   - 官方 `checkToken` 调 `passport.checkWebVerifyForGame()`；
     `_clearUserInfo` 会清空 `stoken`。

当前 vault 是否含 `channel_token` 未知（禁止打开 vault）。旧 vault 允许缺该字段，
所以“能恢复登录”不等于“有 channel_token”。这是当前最强假设。

### 3.4 已确认存在的 passport QR 端点

（空参均返回 `-3005`，证明端点存在；参数仍需最终探测）

- `POST https://passport-api.mihoyo.com/account/ma-cn-passport/web/createQRLogin`
- `POST https://passport-api.mihoyo.com/account/ma-cn-passport/web/queryQRLoginStatus`
- 手机侧：
  - `POST .../account/ma-cn-passport/app/scanQRLogin`
  - `POST .../account/ma-cn-passport/app/confirmQRLogin`
  - `POST .../account/ma-cn-passport/app/cancelQRLogin`

官方安卓 `PorteQRLoginManager` 的流程是：网页/客户端创建 QR（V2 URL），
手机用自己的 passport 登录态 `Cookie: stoken=...; mid=...` 调 `app/scanQRLogin`、
`app/confirmQRLogin`。**网页只负责 create + poll**。

V2 QR URL 形状：

```
https://user.mihoyo.com/mobile.html#/login/qr?tk=<ticket>&token_types=stoken
```

`isV2QRCode` 校验：host 包含 `mihoyo.com`、path 包含 `mobile.html`、
fragment 包含 `/login/qr`；ticket 参数名是 `tk`，token 类型参数是 `token_types`。

### 3.5 passport DS 签名

官方 `createSign`：

```
t = 秒级 Unix 时间
r = 6 位随机串（大小写字母+数字）
b = 实际发送的原始 JSON body（必须逐字节一致）
q = 查询串（通常为空）
DS = t + "," + r + "," + md5("salt=" + SALT + "&t=" + t + "&r=" + r + "&b=" + b + "&q=")
```

生产 salt：`JwYDpKvLj6MrMqqYU6jTKF17KNO2PXoS`
（注意：这是公开在官方包里的常量，不是账号秘密。）

常用头：`x-rpc-app_id`、`x-rpc-device_id`、`x-rpc-game_biz`、
`x-rpc-client_type`、`x-rpc-app_version`、`x-rpc-sdk_version: 2.54.0`、
`x-rpc-account_version: 2.54.0`、`x-rpc-device_fp`、`x-rpc-device_name`、
`x-rpc-device_model`、`x-rpc-sys_version`、`x-rpc-lifecycle_id`。

### 3.6 假设优先级

1. **高**：当前登录来自 Panda Combo QR，缺 passport `stoken` → `channel_token` 为空。
2. **高**：`login_login` 缺 `combo_id/account_type/heartbeat/asterisk_name/data`。
3. **中**：Combo 登录结果本身不完整（Panda 路径与 web `comboWebLogin` 结果字段不同）。
4. **低**：terminal_type 选择。0.2.43 用的是 marker=5（WebPC），
   safe log `STREAM_TERMINAL_TYPE code=5`；默认 `kWebKeyboardTerminal=10` 还没在
   修复登录后对比测试。
5. **已排除**：CPS/UAPC（已修但错误不变）、点击坐标/输入链路（点击确实到达游戏）。
6. **不可控**：账号/区服/云游权限或服务端风控；若登录字段补齐后仍 35-4301，再查这类。

---

## 4. 已确认的协议/代码现状（0.2.43）

### 4.1 输入

- `RtcMouse.cursorX/cursorY`：官方为 [0,1] 归一化；SDK 直接透传，不再乘 1920/1080。
- `RtcTouchEvent.pointX/pointY`：同样 [0,1]；`TouchController` 走
  `RtcDataChannel type=21`、field 13；`RtcTouchEvent` 字段：
  1=fingerId，2=action(0/1/2)，3/4=pointX/Y，5/6=pressSize=0，7=isPrimary，
  8=rtt_trace_send_time；最多 10 条。编码器已实现并有 host 断言；**已接入**：
  `app/src/mihoyo/touch_forward_policy.hpp` 跟踪 Down/Move/Up 与 primary
  （PointerEvent 语义），`Session::send_touch` 由网络循环每 16 ms 批量发送，
  浮球占用的第一根手指被排除，UI 面板打开或退出时释放全部触点；
  host 用例 `mihoyo_touch_forward`。
- 官方 `onMouseMove` 受 `isCursorStatusSynced` 门控；`onMouseDown` 不受门控。
  指针锁定发 `{x:.5,y:.5}` + movement，非锁定发归一化绝对坐标。
- 官方 `mouseButton` 仅写 `cursorX/cursorY`，不写 `buttonType`；
  DataType 4/5=左键，6/7=右键，8/9=中键，10=ZDELTA，11=MOUSE_FEEDBACK。
- 进入点击位置已定：标题页“点击进入”在 y≈0.947；0.2.42-d 与 0.2.43 都证明
  点击到达游戏（2 秒后出弹窗）。
- **弹窗后不要发 Esc**：Esc 会触发 `login_switch_role` →
  `GAME_SDK_SWITCH_ROLE`/`SESSION_ENDED`，会话直接结束。0.2.43 已删除
  Esc/Return/Space 及后续 grid。

### 4.2 CPS / UAPC / terminal

- 官方网页 `getCps` = `` `${platform}_${utm_source||"mihoyo"}` ``；
  WebPC/WebKeyboard → `pc_mihoyo`。
- 官方 `getUapc` = 空字符串。
- 安卓 `BaseSdkHolder`：`info_get_cps` 返回配置值或 `"mihoyo"`；
  `info_get_uapc` 返回 `""`。
- 终端枚举：`CTT_PC=1, CTT_IOS=2, CTT_ANDROID=3, CTT_MAC=4, CTT_WEB_PC=5,
  CTT_WEB_IOS=6, CTT_WEB_ANDROID=7, CTT_WEB_MAC=8, CTT_WEB_TOUCH=9,
  CTT_WEB_KEYBOARD=10`。
- SD marker：`cloudgenshin-terminal.txt=5` = WebPC；
  默认 `kWebKeyboardTerminal=10`。
- 0.2.43 已实现：`BridgeContext.cps`；`DispatchInvoke`/`DispatchInvokeReturn`
  的 cps/uapc 返回值；`network_loop` 按 terminal_type 选
  `pc_mihoyo`/`mac_mihoyo`/`android_mihoyo`/`ios_mihoyo`。

### 4.3 SDK 桥接

- 可靠消息队列 cmd 1900；AES-256-ECB/PKCS7 + Base64；`on_init_response` 等
  包装已与官方 web 对齐。
- `StartGameReq`：1 game_token，2 aid，3 sdk_version，4 terminal_type，6 sid，
  7 pod_id，8 has_rtc_connected(0 省略)，9 since_start_sec，10 link_tasks_ms，
  12 game_config（H.264 空子消息 `62 00`）。
- 官方网页从不发 `RtcVideoState(16)` / `ResetWindowSizeReq(20038)`，故不补。
- `login_login` 当前字段见 3.2；`login_will_enter_game` 返回 `{ret:0,msg:"成功"}`；
  `login_enter_game` 官方无实现，回 `NO_SUCH_METHOD`，游戏接受。
- CRC32c 端序修复保持；SCTP 全链不回退；TLS 校验不削弱。

### 4.4 vault / Authorization

`app/src/mihoyo/launch_flow.hpp`：

```cpp
struct Authorization {
    auth::ComboLoginFields combo;   // app_id, channel_id, open_id, combo_token
    auth::WebDeviceIdentity device; // device_id
    std::string account_id;
    std::string channel_token;      // passport stoken; may be empty
};
```

`app/src/mihoyo/auth_header.hpp` 的 `ComboLoginFields` 只有 4 个字段。

`app/src/mihoyo/login_store.cpp`：

- 加密 vault：`CGLGN001` + 12B nonce + 16B tag + AES-256-GCM ciphertext；
  key 文件 32B；`version:2` 多游戏 entries；最大 16384B。
- `channel_token` 是**可选字段**，旧 vault 缺它仍可加载。
- 解析时 `Field(token,4096)` 校验；`Valid()` 仍要求
  `combo_token` 非空、`account_id` 纯数字、`device_id` 可打印。
- **禁止打开 vault/key 本体**；需要判断 `channel_token` 是否为空时，
  正常 `Load()` 后在内存里 `.empty()`，只发 presence-only 事件。

### 4.5 关键源码位置

- `app/src/mihoyo/sdk_bridge.cpp:594` — `login_login` 返回字段。
- `app/src/mihoyo/sdk_bridge.hpp:56` — `BridgeContext`（含 `cps`、`channel_token`）。
- `app/src/mihoyo/launch_flow.hpp:10` — `Authorization`。
- `app/src/mihoyo/launch_flow.cpp` — QR 登录、`channel_token` 赋值、登录恢复。
- `app/src/mihoyo/qr_login.cpp/.hpp` / `qr_login_http.cpp` —
  `/combo/panda/qrcode/fetch|query`、Account/Combo 载荷解析。
- `app/src/mihoyo/login_store.cpp/.hpp` — vault 读写/版本兼容。
- `app/src/mihoyo/production_profile.hpp` — `GameProfile`、`kWebKeyboardTerminal`、
  app_id/channel_id。
- `app/src/cloudgenshin_stream_view.cpp` — 进入动作表、luma 探测、帧转储、
  terminal/CPS 选择、鼠标归一化。
- `app/src/app_version.hpp` — 版本号。

---

## 5. 下一步（按优先级）

### 第 0 步：先保住现有日志

下一次 `yuzu-run-nosnap.ps1` 启动时会删除 SD 上的 `*.log`。所以下次运行前先把
`cloudgenshin-safe.log`、`sdk-bridge.log`、`frame_thumb.log` 和
`frame_dump_*.bmp` 复制到报告目录（0.2.43 的已经复制）。

### 第 1 步：干净地探测 `createQRLogin`

上次 PowerShell 探测被非交互提示/转义问题打断，没拿到响应。建议新建
`C:\Users\y\Documents\Codex\CloudGenshin-Debug\scripts\passport-qr-probe.ps1`，
用 `curl.exe` + body 文件，确保 DS 的 `b` 与实际 body 逐字节一致；
只打印 `retcode`、`message`、`data` 的 key 名和 URL 的 host/path（ticket 打码），
**不要打印任何 token**。

PowerShell 计算 DS（Windows PowerShell 5.1 可用）：

```powershell
$body = '{"app_id":4,"token_types":["stoken"]}'
$t = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
$chars = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789'
$r = -join (1..6 | ForEach-Object { $chars[(Get-Random -Maximum $chars.Length)] })
$salt = 'JwYDpKvLj6MrMqqYU6jTKF17KNO2PXoS'
$raw = "salt=$salt&t=$t&r=$r&b=$body&q="
$md5 = [BitConverter]::ToString(
  [Security.Cryptography.MD5]::Create().ComputeHash(
    [Text.Encoding]::UTF8.GetBytes($raw))).Replace('-','').ToLower()
$ds = "$t,$r,$md5"
$bodyFile = Join-Path $env:TEMP 'qr_body.json'
[IO.File]::WriteAllText($bodyFile, $body, [Text.UTF8Encoding]::new($false))
curl.exe -sS -X POST 'https://passport-api.mihoyo.com/account/ma-cn-passport/web/createQRLogin' `
  -H 'Content-Type: application/json' -H "DS: $ds" `
  -H 'x-rpc-app_id: 4' -H 'x-rpc-client_type: 5' -H 'x-rpc-game_biz: hk4e_cn' `
  -H 'x-rpc-sdk_version: 2.54.0' -H 'x-rpc-account_version: 2.54.0' `
  -H 'x-rpc-device_id: 0123456789abcdef0123456789abcdef' `
  --data-binary "@$bodyFile"
```

组合尝试（每次只改一个变量）：

- body：`{"app_id":4,"token_types":["stoken"]}`、
  `{"app_id":4,"token_types":["ltoken"]}`、
  `{"app_id":4,"token_types":["stoken","ltoken"]}`；
  以及 `app_id` 为字符串 `"4"`、加 `"device"` 字段的变体。
- 头：`x-rpc-client_type` 试 `4`（PC）、`5`（Web）、`19`（移动 web）等。
- 如果 `createQRLogin` 成功，响应里应有 ticket / url（或 `data.url`）；
  用它渲染 QR（V2 URL），轮询 `web/queryQRLoginStatus`（body `{ticket}`）。
- 若 `createQRLogin` 一直 -3005/其他错误，记录完整 `retcode/message`（不含 token），
  再考虑用 passport 网页登录的 iframe/密码/短信路径，或从官方 web bundle 里
  找 `passport` 模块的真实 create 参数。

### 第 2 步：实现 passport QR → comboWebLogin（0.2.44）

建议新增 `app/src/mihoyo/passport_qr_login.cpp/.hpp`（或扩展现有 `qr_login`），
职责：

1. `createQRLogin`（web）→ 拿 ticket + URL。
2. 用现有 QR 渲染器显示 URL（不打印 URL/ticket 到日志）。
3. 轮询 `queryQRLoginStatus`：`Created`/`Scanned`/`Confirmed`/`Used`/`Expired`；
   用户取消要能回到登录页。
4. 从 Confirmed 响应里取 passport token（stoken）和 uid/aid；
   **只保存在内存/Authorization，不打印**。
5. 带 passport cookie（至少 `stoken`、`mid`；有 `ltoken`/`cookie_token`/`account_id`
   就一起带）POST `comboWebLogin`：
   `{"app_id": <profile app_id>, "channel_id": <profile channel_id>}`。
6. 解析 combo 登录结果：
   - 顶层字段（`combo_id`、`open_id`、`combo_token`、`account_type`、
     `asterisk_name` 等）
   - 内层 `data`（JSON 字符串）解析后展开
   - `heartbeat`（若顶层/内层有）
7. 组装 `Authorization`：
   - `combo.app_id/channel_id/open_id/combo_token` 保持现有
   - `channel_token = stoken`
   - 新增可选字段保存 `combo_id/account_type/heartbeat/asterisk_name`
     和 `data` 展开结果（见第 3 步）
8. 保存 vault（保持 version 2；旧 vault 仍可读）。

### 第 3 步：扩展 login_login 与 vault

- `login_login` 的 `data` 合并：
  - 本机：`device_id`、`app_id`、`channel_id`、`open_id`、`combo_token`
  - 官方安卓 `UserData.toDataJSON()` 白名单：`combo_id`、`account_type`、
    `heartbeat`、`asterisk_name`
  - `data` 里的键展开（只接受标量/对象，限制总大小；不要原样无限透传）
  - `channel_token` 仅在非空时加入（当前逻辑已是）
- 需要一个 JSON value 版 `put`，不要只支持 string；`heartbeat` 是 bool，
  `combo_id` 可能是数字/字符串（按官方响应类型保留）。
- 扩展 `ComboLoginFields`（或 `Authorization`）加可选字段；
  `login_store.cpp` 的 `SerializeEntry`/`ParseEntry` 同步加可选字段，
  保持 `version:2` 和旧 vault 兼容，最大 16384B 限制不变。
- 加 presence-only 诊断事件（只报 0/1）：
  - `LOGIN_CHANNEL_TOKEN_PRESENT`
  - `LOGIN_COMBO_ID_PRESENT`
  - `LOGIN_HEARTBEAT_PRESENT`
  - `LOGIN_ACCOUNT_TYPE_PRESENT`
  - `LOGIN_ASTERISK_NAME_PRESENT`
  - `LOGIN_COMBO_DATA_KEYS_COUNT`（只报数量）
  **绝不打印值。**
- 若 `channel_token` 为空：`ResumeLogin` 返回 `Rejected` 并触发重新扫码
  （迁移用）；不要静默继续用旧 vault 进游戏。

### 第 4 步：构建/测试/实机

1. 构建 0.2.44（见第 6 节命令；先停 yuzu，避免 “Failed to open output file!”）。
2. host 测试：
   `mihoyo_game_control`、`mihoyo_rtc_session`、`mihoyo_input_packet` 必须 PASS。
3. 保留 `cloudgenshin-autotest.click`，让进入探测继续跑；用户用手机扫新的
   passport QR（手机需已登录米游社/passport，不是“云原神扫码登录”的 Combo 确认）。
4. 读 `frame_dump_*.bmp` 判断 35-4301 是否消失；不要靠截图/抢焦点。
5. 若仍 35-4301：
   - 先看 presence 事件，确认 `channel_token`/字段是否真的进了 `login_login`。
   - 再用 `cloudgenshin-terminal.txt=10`（WebKeyboard）对比一次；
     也试 5（WebPC）。
   - 检查 `login_login` 的实际 JSON key 集合（sdk-bridge 只记 keys，不记值）。
   - 若字段齐全仍失败，转向账号/区服/云游权限/风控，不要继续改输入层。

### 第 5 步：成功后

- 删除 SD 的 `cloudgenshin-autotest.click`（恢复“默认永不自动点击”）。
- 把 0.2.42/0.2.43/0.2.44 结论补进
  `docs/cloudgenshin-integration-status.md`。
- 切 `cloudgenshin-game.txt` 到 `hkrpg`（首次需用户扫码），再 `nap`。
- 全程保持 720p/4000/FPS30、双语极简 UI、登录/排队/登出无回归；
  不提交、不 push。

---

## 6. 关键命令 / 路径 / 标记

### 构建

```powershell
C:\devkitPro\msys2\usr\bin\bash.exe -lc "cd /c/Users/y/Documents/Codex/2026-09-22/da-ji/work/CloudGenshin-NX-native && bash scripts/build-switch-msys2.sh"
```

输出：`build/switch/SwitchNOW.nro`

注意：yuzu 运行时构建会报 “Failed to open output file!”，先停 yuzu。

### host 测试

```powershell
cd C:\Users\y\Documents\Codex\2026-09-22\da-ji\work\CloudGenshin-NX-native
.\scripts\test-mihoyo-host.ps1 -TestNames @('mihoyo_game_control','mihoyo_rtc_session','mihoyo_input_packet')
```

`scripts/test-streaming-host.sh` 在 `websocket_handshake` 因 `constant_time.o`
未解析 `mbedtls_md_*` 失败，是环境/链接问题，与本改动无关。

### yuzu 后台运行

```powershell
C:\Users\y\Documents\Codex\CloudGenshin-Debug\scripts\yuzu-run-nosnap.ps1 `
  -ReportDirectory 'C:\Users\y\Documents\Codex\CloudGenshin-Debug\reports\gui-0.2.44-1' `
  -RunSeconds 600
```

- 默认 yuzu：`H:\ALL\yuzu-windows-optimized-pgo-ea-4176\yuzu.exe`
- 脚本启动时删除 SD 上 `*.log`；运行结束才复制到报告目录。
  中途被杀（例如用户 t=182s 停止）时，日志仍在 SD，需手动复制。
- 纯后台，不抢焦点，不截图。

### 路径

| 内容 | 路径 |
|---|---|
| 主仓库 | `C:\Users\y\Documents\Codex\2026-09-22\da-ji\work\CloudGenshin-NX-native` |
| 官方 web 参考 | `...\da-ji\reference\web\ys\combo-web.182d5437.js`、`home.35c0bfdb.js`、`web.b959658b.js` |
| 安卓参考 | `...\da-ji\reference\android\yscloud-7.1.0-3\jadx\sources` |
| 调试目录 | `C:\Users\y\Documents\Codex\CloudGenshin-Debug` |
| SD 运行目录 | `...\CloudGenshin-Debug\runtime\sdmc\switch\SwitchNOW` |
| 0.2.43 证据 | `...\CloudGenshin-Debug\reports\gui-0.2.43-cps-1` |
| 弹窗放大图 | `C:\Users\y\AppData\Local\Temp\opencode\cgframes` |

### SD 标记

| 文件 | 内容 | 说明 |
|---|---|---|
| `cloudgenshin-autotest.txt` | `CGNX_AUTOTEST_V1` | 开启自动测试 |
| `cloudgenshin-game.txt` | `hk4e` / `hkrpg` / `nap` | 当前游戏 |
| `cloudgenshin-autotest.click` | `CGNX_ENTRY_CLICK_V1` | 显式允许一次进入点击；成功后删除 |
| `cloudgenshin-terminal.txt` | `5` / `10` | 5=WebPC，10=WebKeyboard |
| `cloudgenshin-language.txt` | `zh` / `en` | UI 语言 |
| `cloudgenshin-login.vault` | 加密 | **禁止读取/打印** |
| `cloudgenshin-login.key` | 32B | **禁止读取/打印** |

---

## 7. 迁移清单（换机器/换会话）

### 必须复制

1. **整个主仓库目录**（不要只靠 git）：
   `C:\Users\y\Documents\Codex\2026-09-22\da-ji\work\CloudGenshin-NX-native`
   - 包含未提交改动和未跟踪文件；**不要 `git clean/reset`**。
   - 重点未跟踪：`app/src/mihoyo/vlink_session.cpp/.hpp`、
     `app/src/cloudgenshin_*.cpp/.hpp`、`app/src/endfield/` 等。
2. **官方参考目录**：
   `C:\Users\y\Documents\Codex\2026-09-22\da-ji\reference`
   - 体积大；至少保留 `web/ys` 和 `android/yscloud-7.1.0-3/jadx/sources`。
3. **调试目录**：
   `C:\Users\y\Documents\Codex\CloudGenshin-Debug`
   - 含 `scripts/`、`runtime/sdmc/`、`reports/`。
   - **排除**所有 `cloudgenshin-login.vault`、`cloudgenshin-login.key`、
     `endfield-login.vault`、`endfield-login.key`，以及任何 token dump。
4. **本交接文档**（仓库 `docs/` 和当前工作目录副本）。

### 复制命令示例（先确认目标路径，不要覆盖错目录）

```powershell
$dst = 'E:\CloudGenshin-Migrate'   # 改成目标盘/目录
robocopy 'C:\Users\y\Documents\Codex\2026-09-22\da-ji\work\CloudGenshin-NX-native' `
  "$dst\work\CloudGenshin-NX-native" /E /R:2 /W:2 `
  /XF cloudgenshin-login.vault cloudgenshin-login.key `
      cloudgenshin-login.vault.bak cloudgenshin-login.key.bak `
      endfield-login.vault endfield-login.key

robocopy 'C:\Users\y\Documents\Codex\2026-09-22\da-ji\reference' `
  "$dst\reference" /E /R:2 /W:2

robocopy 'C:\Users\y\Documents\Codex\CloudGenshin-Debug' `
  "$dst\CloudGenshin-Debug" /E /R:2 /W:2 `
  /XF cloudgenshin-login.vault cloudgenshin-login.key `
      cloudgenshin-login.vault.bak cloudgenshin-login.key.bak `
      endfield-login.vault endfield-login.key
```

不要用 `/MIR`（会删除目标端多余文件）。复制后检查：
`$dst\work\CloudGenshin-NX-native\app\src\mihoyo\vlink_session.cpp` 存在，
且目标目录里没有 vault/key。

### 不要复制 / 单独处理

- vault/key：如果目标机器仍是同一可信用户、且希望免扫码，才通过安全通道单独复制；
  绝不提交、绝不打印、绝不放进共享目录。否则让目标机器重新扫码。
- SD 上的旧日志可以复制，但不要覆盖目标机器的新运行日志。
- 不要把 `cloudgenshin-autotest.click` 带到生产 SD；测试完删除。

### 目标机器需要

- Windows + PowerShell + `curl.exe`。
- devkitPro/msys2，默认路径 `C:\devkitPro\msys2`；若不同，改
  `scripts/build-switch-msys2.sh` 和构建命令。
- yuzu（含 Switch keys/firmware），路径与 `yuzu-run-nosnap.ps1` 的 `-Yuzu` 一致；
  目标机器路径不同就传参。
- SD 布局：`...\CloudGenshin-Debug\runtime\sdmc\switch\SwitchNOW`；
  若 yuzu 的 SD 路径不同，改脚本里的 `$sdRoot`。
- 可访问 `passport-api.mihoyo.com`、`hk4e-sdk.mihoyo.com`、云游信令/节点。
- 手机安装官方米游社/云原神 App，并已登录要使用的账号（passport 扫码确认用）。

### 迁移后第一次验证

1. `git -C <repo> status --short` 确认改动还在（只读，不改）。
2. 构建 0.2.43 原样，确认能复现；再改 0.2.44。
3. 保留旧 vault 先跑一次，看 presence 事件里 `channel_token` 是否为空；
   为空则按计划触发重新扫码。
4. 实机只后台跑，读日志/帧转储，不截图。

---

## 8. 安全与操作注意事项

- 绝不打开/打印 `cloudgenshin-login.vault`、`cloudgenshin-login.key`、
  `endfield-login.*`、任何 `Authorization` 对象、任何 HTTP body 里的 token。
- 只允许记录：函数名、字段名、key 名、presence 0/1、字节数、retcode、
  HTTP 状态、URL 的 host/path（ticket 打码）。
- TLS/证书/指纹校验保持开启；不得为了“能连”而关闭或降级。
- 不自动购买、不自动切队列、不绕验证码；进入点击只在 marker 存在时执行一次。
- 默认不 push；不运行 `git clean`/`git reset --hard`。本地提交前先检查敏感文件，
  vault/key 永不入库。
- 不要用 PrintWindow/Ctrl+P/SendKeys/抢焦点；yuzu 纯后台。
- 不要发 Esc 关闭错误弹窗（会触发 `login_switch_role` 结束会话）。

---

## 9. 给下一个会话的开场提示（可直接粘贴）

> 继续 CloudGenshin-NX-native（仓库 `C:\Users\y\Documents\Codex\2026-09-22\da-ji\work\CloudGenshin-NX-native`，
> 版本 0.2.46，禁止 clean/reset --hard）。先读
> `docs/cloudgenshin-handoff-2026-10-21.md` 和
> `docs/fix-35-4301-2026-10-21.md`。
>
> **原神 35-4301 已结案**：旧 vault 缺 `account_type`；0.2.46 的
> `ResolveLegacyGenshinAccountType` 仅对 `app_id=4/channel_id=1/open_id==account_id`
> 的国服官方 hk4e 身份补 `1`。已用同一 264 B 旧 vault 在 yuzu 后台实测：
> `GAME_SDK_LOGIN_LEGACY_ACCOUNT_TYPE_RESOLVED=1`、无 35-4301、
> `GAME_SDK_WILL_ENTER_GAME`，帧转储可见角色/地图/HUD。
>
> 下一步：保持默认普通队列、不自动购买、不切队列；先验证 hkrpg（星穹铁道），
> 再验证 nap（绝区零）。两者没有 hk4e 旧存档，首次需用户各扫码一次。
> `account_type` legacy 恢复不得跨游戏复用。测试进入仍需显式
> `cloudgenshin-autotest.click`，成功后立即删除；实机只后台运行、读安全日志和
> 帧转储，不截图/抢焦点，不打印 vault/key/token，不削弱 TLS。

---

## 10. 0.2.35–0.2.43 本地提交状态

已在 2026-10-21 做本地 checkpoint 提交（commit `1aef982`，未 push），包含：

- 修改：`CMakeLists.txt`、`app/src/WebSocketClient.*`、`app/src/app_version.hpp`、
  `app/src/http_client.*`、`app/src/main.cpp`、`app/src/main_activity.cpp`、
  `app/src/mbedtls_timing_compat.c`、`build-switch.ps1`、
  `extern/libpeer/src/*`、`scripts/build-switch-msys2.sh`、
  `scripts/test-streaming-host.sh`、`tests/*`。
- 未跟踪：`app/src/cloudgenshin_platform_init.c`、
  `app/src/cloudgenshin_stream_input.cpp`、`cloudgenshin_stream_overlay.cpp`、
  `cloudgenshin_stream_view.cpp/.hpp`、`cloudgenshin_tab.cpp/.hpp`、
  `app/src/endfield/`、`app/src/mihoyo/vlink_session.cpp/.hpp` 等。

这些是 0.2.35–0.2.43 的工作成果，已包含在本地 commit `1aef982` 中；迁移时仍建议整目录复制，
不要把 vault/key 放进仓库。

---

## 11. 2026-10-21 passport QR 实测结论

通过官方云原神页面 `https://ys.mihoyo.com/cloud/` 的登录 iframe 实际抓包确认。
只记录字段名/头名/URL host+path，ticket/token 一律打码。

### 11.1 createQRLogin

- `POST https://passport-api.mihoyo.com/account/ma-cn-passport/web/createQRLogin`
- body 就是 `{}`（2 字节，`Content-Type: application/json`）。
- 请求头（公开客户端参数，不是账号凭据）：
  - `x-rpc-app_id: c76ync6mutq8`
  - `x-rpc-client_type: 22`（CloudWebPC）
  - `x-rpc-game_biz: hk4e_cn`
  - `x-rpc-sdk_version: 2.57.0`
  - `x-rpc-device_id`（36 字符 UUID）
  - `x-rpc-device_fp`（13 字符）
  - `x-rpc-device_model: Electron%2044.4.5`
  - `x-rpc-device_name: Electron`
  - `x-rpc-device_os: Windows%2010%2064-bit`
  - `x-rpc-lifecycle_id`（10 位十六进制）
  - `x-rpc-mi_referrer`（当前 login-platform URL）
  - `Origin: https://user.mihoyo.com`、`Referer: https://user.mihoyo.com/`
  - 浏览器 `withCredentials: true`
- **没有 DS 头**。此前空参探测得到 `-3005`，原因是缺官方头/body 形式，不是 DS。
- 成功：`retcode=0, message=OK, data={url,ticket}`。
  - `data.url`：`https://user.mihoyo.com/login-platform/mobile.html?expire=...&tk=...&token_types=...`
  - `data.ticket`：36 字符
  - 响应无 `Set-Cookie`
- 官方路由 `#/login/qr`；组件 mount 时调用 `createQRLogin({})`，二维码由 `data.url` 生成。

### 11.2 queryQRLoginStatus

- `POST .../web/queryQRLoginStatus`，body `{"ticket":"<36 字符>"}`，每 2 秒一次。
- `data` 固定字段：
  `status, app_id, client_type, created_at, scanned_at, tokens, user_info, realname_info, need_realperson, ext, scan_game_biz`
- `status`：`Created` → `Scanned` → `Confirmed`。
- 未确认时 `tokens` 为空；`Confirmed` 后填充。请求 `token_type=4`（CookieToken），
  因此 tokens 里是 cookie token 相关字段；具体结构等实际扫码确认后再记录，绝不打印值。
- 官方 Confirmed 后把整个 `data` + ticket 传给 `onSuccess`。
- 官方代码随后调用
  `POST https://passport-api.mihoyo.com/account/ma-cn-session/web/webVerifyForGame`，
  返回 `{userInfo, realnameInfo, token, needRealperson}`；`token` 为 stoken
  （`token_type=1`），`_saveToken` 存入 `stoken`。
- 再调用 `POST https://hk4e-sdk.mihoyo.com/hk4e_cn/combo/granter/login/webLogin`；
  官方 `_createComboLoginSuccResult` 里 `channel_token = stoken`，并把 `e.data`
  （JSON 字符串）与其余字段合并。

### 11.3 官方登录 iframe 参数

`https://user.mihoyo.com/login-platform/index.html?...`

- `client_type=22`
- `app_id=c76ync6mutq8`
- `theme=ys`
- `token_type=4`
- `game_biz=hk4e_cn`
- `message_origin=https%253A%252F%252Fys.mihoyo.com`
- `succ_back_type=message%3Alogin-platform%3Alogin-success`
- `fail_back_type=message%3Alogin-platform%3Alogin-fail`
- `ux_mode=popup`
- `iframe_level=1`
- `extra_trace=1`

`loginPlatform.appId` 来自 `web.b959658b.js`；`client_type=22` 是 CloudWebPC，
不是 SDK 的 `WebKeyboard=25`。

### 11.4 下一步实现（0.2.44）

1. 在 `app/src/mihoyo/` 增加 passport QR 模块：
   - create（上述头/body）→ 保存 ticket/url → 渲染二维码；
   - 2 秒轮询；处理 `Created/Scanned/Confirmed`、过期/取消/风控 retcode；
   - device_id/lifecycle_id 生成一次并持久化（不是 token，可放配置）。
2. Confirmed 后先确认 `webVerifyForGame` 的 token 传递方式（cookie 还是 body/header），
   再实现 stoken 获取；不要把 token 写日志。
3. `comboWebLogin` 请求体/凭据按官方 `combo-web` 实现；解析 `data` JSON 字符串。
4. 扩展 `ComboLoginFields`/`login_store`（v3，向后兼容 v2）：可选 `channel_token`、
   `combo_id`、`account_type`、`heartbeat`、`asterisk_name`，以及 `data` 展开字段。
5. `login_login` 保留本机 `device_id/app_id/channel_id/open_id/combo_token`，
   `channel_token` 仅非空时加入；保留 bool/数字类型；加 presence-only 事件。
6. 旧 vault 先跑一次看 `LOGIN_CHANNEL_TOKEN_PRESENT`；为空则触发重新扫码。
7. 构建 0.2.44，host 测试，yuzu 后台实机；用户扫 passport QR；读帧转储确认 35-4301。

