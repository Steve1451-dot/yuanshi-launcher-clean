# 云·终末地 (Endfield Cloud) 协议侦察 — Findings

日期：2026-10-17
来源：官方 web 串流客户端 bundle（`reference\web\endfield\`），全部常数均来自官方产物，无猜测值（未确认项已标注）。
关联：`docs\cloudgenshin-integration-status.md`（项目总状态）。

## 0. 结论摘要

- 云·终末地 = 鹰角发行后台（`cloud-game.hypergryph.com`）+ 蔚领时代 Vlinker/WeLink PaaS（`vlinkcloud.cn`）+ 标准 WebRTC。
- 串流主流程（prod 默认，`isOldGS=false`）：
  1. 鹰角通行证 OAuth 登录（oauth appCode=`dd7b852d5f1dd9da`）→ hg_token
  2. `POST /api/v1/user/exchange {hg_token,u8_token,epk_jwk}`（ECDH P-256）→ `login_session_id`(Bearer) + `tenant_key/host_url/game_id/tenant_params`（`vlink_tenant_data` 为 JWE，用本地私钥解密）
  3. `POST /api/v1/session/dispatch` → 排队 → `GET /api/v1/session/poll/{session_id}` → `sdk_msg`(JSON 字符串，内含 gsIp/webRtcPort/gsmToken/userId 等)
  4. WebRTC：客户端 offer → `wss://webrtc-signaling.vlinkcloud.cn/ws-proxy`（先发 `{"gsUrl":"ws://<gsIp>:<webRtcPort>"}`，收到 "connected to gs" 后发 offer，收到 `{code:0,answer:{sdp}}` 后改写候选 IP 再 setRemoteDescription）→ 数据通道 `"welink"`
  5. 数据通道：SimpleCmd protobuf 双向；服务器 "protoVersion" 后客户端发 SDPInfo（authV2）；输入用 OneInputOPData protobuf；2s 心跳
- API 签名：HMAC-SHA256(key = `"Bearer "+session` 或空串, msg = `METHOD\nPATH\nSORTED_QUERY\nTIMESTAMP\nSHA256HEX(BODY)`)。

## 1. 证据文件（reference\web\endfield\）

| 文件 | 大小 | 内容 |
|---|---|---|
| `cloud.html` | 10,251 | 官方页面入口（含 webp 探测等） |
| `index.cda000.js` | 3,720,072 | 主 bundle：登录 UI、API、签名、WeLink SDK、proto、输入表、全部协议 |
| `commons.80b897.js` | 246,533 | 公共 chunk（无协议关键内容） |
| `index.e8b5f2.css` | 636,911 | 样式 |
| `hg-account-web-sdk.min.js` | 455,543 | 鹰角账号 SDK v3.5.0（登录/令牌；QR 在服务端页面内） |
| `hg-web-sdk.min.js` | 73,034 | 鹰角 web 埋点 SDK v3.0.3 |
| `sdk.entry.js` | 7,465 | SDK 加载器（定位到上述两个 SDK） |
| `d9d0.json` | 5,821 | 官方站点 Lottie 动画资源（与协议无关） |
| `extract-2..27.txt` | — | 各轮抽取原文（含偏移） |

版本标识：web 客户端 `"1.6.0"`；WeLink web SDK `"5.14.1.260916160035-wlweb-release"`；账号 SDK `3.5.0`。

## 2. 域名与 prod 常量

- `CLOUD_SERVER_URL = https://cloud-game.hypergryph.com`（业务 API base）
- `ACCOUNT_SERVER_URL = https://as.hypergryph.com`；`GAME_CONFIG_SERVER_URL = https://game-config.hypergryph.com`
- 账号：`https://user.hypergryph.com`（通行证/账号 SDK）、`u8.hypergryph.com`（u8 票据）、`web-api.hypergryph.com`
- 信令：`wss://webrtc-signaling.vlinkcloud.cn`（`configs.signalDomain`，默认值硬编码于 SDK）
- 其他：`transfer-inner.wl-times.com`（hashGWHost）、`m1.openfpcdn.io`（FingerprintJS）、`web.hycdn.cn`、`cloud-game-web-static.hycdn.cn`（静态资源）
- 环境枚举（bundle @733541）：`a="prod"`（已确认当前线上为 prod）
- prod 表（ENDFIELD 项）：

| 名称 | 值 | 用途（已确认/推断） |
|---|---|---|
| serverAppCode（X-APP-CODE） | `be6ybz1j0UpYIS8V` | 所有 cloud-game API 请求头 |
| oauth appCode | `dd7b852d5f1dd9da` | `User.UI.login({oauth:{appCode,grantType:0}})` |
| P (iz) | `tl1x4hcuyolbci8innylddig` | 用途待确认（疑似埋点/上报键） |
| C (hq) | `twnx1lgk3fjxov78n6n74ju3` | 用途待确认 |

注：`sdk_msg`/`x-app-code` 环境位：testin/inner_test/stable/pre/prod/audit 全部已抽取（见 extract-3/11），默认段 `6xyvp5MLL5bDzhPW`。

## 3. API 头与签名（bundle @1824xxx / @1792613）

请求头（tZ）：

- `X-Signature`：`hmac_sha256_hex(key, msg)`
  - `key = session ? "Bearer "+session : ""`（session=`login_session_id`；未登录时为空串）
  - `msg = [METHOD.toUpperCase(), pathname, sortedQuery, timestampStr, sha256hex(bodyStr)].join("\n")`
  - `bodyStr = data==null ? "" : (typeof data==="string" ? data : JSON.stringify(data))`
  - `sortedQuery` = `new URLSearchParams(searchParams).sort()`（按 key 再 value 排序）后的 `toString()`（无查询参数时为空串）
- `X-Timestamp`：`floor(Date.now()/1000 - serverOffset)`（serverOffset 由响应 `timestamp` 校准，`tj=300s` 过期重试）
- `X-APP-CODE: be6ybz1j0UpYIS8V`
- `X-Client-App-Version: 1.6.0`；`X-Client-Platform: web`；`X-Client-Device-Model`、`X-Client-Os-Version`（UA 派生）
- `X-Client-Device-Id: <deviceId>-<fingerprint>`（浏览器环境才加，Xbox/Switch 实现可省略或伪造稳定值）
- `Authorization: Bearer <login_session_id>`（登录后）

实际 HTTP 响应为 `{code,msg,data,timestamp}`（`code=0` 成功）；Web 请求包装器将它转换为 `{data,timestamp,error:{code,msg},isSuccess}`。原生解析同时兼容两种形式，不能要求服务器返回包装器的 `isSuccess` 字段。错误码：

- `-1/408/5xx`：重试一次（~300ms）
- `12006/12007/12008/12010`：登录失效 → 重新登录
- `12002`：服务器异常；`12108`：无资格；`21003`：当前节点排队人数过多（可换节点）
- exchange 专用：`-20001` 游戏配置解密失败

## 4. 登录（账号 SDK）

- 集成点（bundle @1283xxx / @1332xxx）：`User.UI.login({oauth:{appCode:"dd7b852d5f1dd9da",grantType:0}}, cb)`；成功后 `User.getToken().data.token` = **hg_token**。
- **2026-10-24 更正**：`et(e,t)` 的空串是缺省值，不能视为有效登录。它的调用者 `ex()` 先做 `User.grantSilent(cloudApp,0)`，再以授权 code 调用 `U8.API.tokenByOauth2Code`，最后将 **grant 返回的 token** 与 **U8 token** 传给 exchange。实测空 U8 返回 11204。
- 保存的扫码 passport token 不是上述 cloud grant token。SDK 还提供 `User.grantApp(cloudApp)`（`type=1`）及 `U8.API.authByHG`（`channelToken:{type:0,token:passport}`）。2026-10-24 已用旧加密存档验证两步均 `status=0`，无需绑定角色 uid 或再次扫码。详见后续验证记录。
- QR 登录在通行证页面内部完成（SDK 只拿到结果）；账号 API 端点（账户 SDK 内，供后续原生 QR 实现）：
  - `user.hypergryph.com`：`/user/auth/v1/token_by_phone_code`、`/user/auth/v1/token_by_phone_password`、`/user/auth/v1/token_by_scan_code`、`/user/auth/v1/token_by_exchange_code`、`/user/info/v1/authenticate`、`/user/info/v1/basic`、`/user/info/v1/token_by_cookie`、`/account/info/`、`/account/binding/v1/token_by_uid`、`/account/binding/v1/u8_token_by_uid` 等
- **MVP 登录方案（已定）**：首版用「PC 浏览器登录一次 → 手工录入 hg_token（vault 保存）→ 自动 exchange」；QR 原生实现后补（端点已列出，`token_by_scan_code` 是扫码换票关键）。

## 5. exchange（bundle @1283802 / @1827359）

请求：`POST /api/v1/user/exchange`，data = `{hg_token, u8_token, epk_jwk}`

- `epk_jwk`：客户端 WebCrypto `ECDH P-256` 密钥对（`generateKey({name:"ECDH",namedCurve:"P-256"}, true, ["deriveKey","deriveBits"])`）的**公钥 JWK JSON 字符串**。
- 第 2 参 `{showDefaultErrorDialog}` 为 UI 选项，签名不包含。

响应 data（成功）关键字段：

- `login_session_id`（→ Bearer）
- `server_id`、`gender`
- `tenant_key`、`host_url`、`game_id`、`tenant_params`
- `vlink_tenant_data`（可选）：JWE 紧凑序列（jose 库解密，客户端用私钥 `compactDecrypt`），明文 JSON 含 `{tenant_params, tenant_key, host_url, game_id}` 覆盖以上字段
- 解密失败 → 错误 `-20001`

2026-10-24 节点流程补充：当前页面调用 `p.getNodeList(callback,error)`，SDK 没有接收实例游戏参数，使用官方配置 CDN `https://paas-sdk-config.vlinkcloud.cn`（备用 `https://paas-sdk-config-ks.vlinkcloud.cn`）的 `/ping/<md5(tenantKey+"H5")>.html`，读取 `lines`。`tenant_params` 实测为不透明串，不能套用 SDK 的 `dO()` 拆分后构建游戏节点路径。云派发的 `gameId` 直接使用 exchange 返回的 `game_id`，与配置 CDN 路径参数不是同一个契约。

注：JWE 具体 `alg/enc`（ECDH-ES(+A128KW?) 与 A128CBC-HS256/A256GCM）在 bundle 里是 jose 全量支持表，实际参数需首次实测确认；实现两种主流组合备选。

## 6. 会话/排队（bundle @811xxx / @1827620）

- `POST /api/v1/session/dispatch` data（bundle @811924）：

```js
{ tenant_key, node, gameId, resolution: `${w}x${h}`, bit_rate, fps: 60,
  codec_type, cmd_line, pay_type, version, biz_data }
```

- 响应/轮询字段（@812719）：`session_id, status, queuing_size, queuing_seat_no, sdk_msg, estimated_wait_time, user_level`
- 轮询：`GET /api/v1/session/poll/{session_id}`（`queuePeriod = 10000ms`）
- 取消：`POST /api/v1/session/cancel {session_id}`；换队列：`POST /api/v1/session/change_queue`
- `sdk_msg`：JSON 字符串 → `sdkData`，含（多来源汇总，实测校对）：
  `gsIp`（单线=“ip”，多线=JSON 数组 `[{lineIp,port,type}]`）、`webRtcPort`（或 `gsPort`）、`gsmToken`、`userId`、`playerId`、`playerRole`、`codecType`(数字, 18=H264, 21=H265?)、`gsId`、`nodeId`、`recordId`、`ext`、`gameId`、`instanceId`、`auth`、`verify`、`packedData`、`assignResultParam`
- 其他 API：`/api/v1/user/wallet_info|fetch_messages|ack_messages|consume_history`、`/api/v1/game/node_list`、`/api/v1/banner/get`、`/api/v1/cloud_game_store/*`
- 钱包类型枚举：FREE_MINUTES:1、PAY_MINUTES:2、TRIAL_PLAY_CARD:3、PLAY_CARD:4、PAY_TOKENS:99

## 7. WebRTC 信令（bundle @1919000-@1925500）

模式选择：`isOldGS`（configs）→ `r.Mx`。**prod web 默认 isOldGS=false → 客户端 offer 模式**。

### 7.1 客户端 offer（默认）

1. `RTCPeerConnection`；`addTransceiver("video",{direction:"sendrecv"})`、`addTransceiver("audio",sendrecv)`；`createDataChannel("welink")`；`createOffer({iceRestart:true})`（非 trickle：offer 里带候选）
2. offer 修饰 S()：为 opus fmtp 追加 `;stereo=1`；按 codecType 过滤（18=H264 时剔除 H265 行）等
3. `new WebSocket(configs.signalDomain + "/ws-proxy")`（`wss://webrtc-signaling.vlinkcloud.cn/ws-proxy`）
4. 开：`{"gsUrl":"ws://<gsIp>:<webRtcPort>"}`（proxy-arm 时追加 `/webrtc-proxy`）
5. 收 `{code:0, message:"...connected to gs..."}` → 发 `{"type":"offer","sdp":"..."}`（RTCSessionDescription 序列化）
6. 收 `{code:0, answer:{type:"answer", sdp:"..."}}` → 改写 answer 候选（见 7.3）→ `setRemoteDescription`
7. 失败日志键：`SERVER_CONNECT_FAILED / SIGNAL_SUCCESS / SERVER_REQUEST_ENABLE / SERVER_CONNECT_SUCCESS`

### 7.2 服务端 offer（isOldGS=true，备用路径）

- `new WebSocket(configs.socketUrl)`；开：`{"auth":gsmInfo.auth,"verify":gsmInfo.verify}`；`onicecandidate` → `{"type":"answer_ice","iceCandidate":{...}}`
- 收 `{"type":"offer","sdp":...}` → `setRemoteDescription` → `createAnswer` → 发 answer（JSON）→ `setLocalDescription`

### 7.3 SDP 候选改写（@1917612）

收到 answer 后把候选统一改写到中继地址：`(sdp, ip=currentIp/gsIp, port=webRtcPort)`

- 一般：替换候选行中的 IP/端口（`BH`，`a=candidate` 行内替换）
- proxy-arm：整行替换为 `a=candidate:1221718136 1 udp 1694498815 <ip> <port> typ srflx raddr 0.0.0.0 rport <port> generation 0`（`KH`）

## 8. 数据通道协议（label `"welink"`，protobuf）

### 8.1 SimpleCmd（WLCGV2.SimpleCmd）

| field | 类型 | 含义 |
|---|---|---|
| 1 | string | cmd |
| 2 | string | arg |
| 3 | bytes | data |
| 4 | string | arg1 |
| 5 | string | key(?) |

解码（收）：`Qm(buffer)` → `.j`（按字段序的 bytes 列表）→ `j[0]`=cmd、`j[1]`=arg（UTF-8）。

### 8.2 上行（客户端 → 服务器）

- **ChannelType**（canned 字节，非 proxy-arm 时在 protoVersion 后发）：

```
0A 0B "ChannelType" 12 09 "主通道" 1A 09 "301500100" 22 01 "30" 2A 02 "32 33"
```

- **authV2**（SDPInfo，见 8.4）：`SimpleCmd{cmd:"authV2", arg:JSON}`
- **intercept_app**：`{"transFile":true,"transJson":true,"transTxt":false}`（arg）
- **CtrlMsgV2**（手柄全量）：data = `WLCGV2.InputOPData{op_list:[OneInputOPData{...}]}`；tag `gamePadEventXbox` / `gamePadEventPs4` / `gamePadEvent`
- 单点输入走 `CG.PROTO.OneInputOPData` 直发（见第 9 节）：tag `Keyboard` / `mouseEvent` / `mousemove` / `gamePadEvent` / `keepAliveForGame`
- 心跳（修正，详见 §14.1/§14.4）：
  - canned 字节（`0A 09 三汉字 12 0D 数字串 22 01 "34"`，精确字节见 §14.4）→ tag `heartBeat`，**DC 打开后每 2s**
  - `SimpleCmd{cmd:"SDKHeartBeat"}`（无参）→ tag `gsProxy heartBeat`，**收到"游戏开始"后每 1s**
- **pause/play**：`changeAVC` arg="0"/"1"
- 其他：`ClientOnline`（SimpleCmd{cmd:"ClientOnline", data:{account_id, cmd_line, cursor, extparam, gamepath, cfgpath, sdk_info}}，发在 Direct 路径）、`SetFps`、`SetCursorMode`、`ResetVideoBitrate`、`autoBitrateAdjust`、`SetVideoArea`、`SendDataToGame`、`DataToGameWithKey`、`BusinessDataConfirm`、`StateSync`

### 8.3 下行（服务器 → 客户端）

- `protoVersion`：首次握手（只处理一次）。客户端后续动作（bundle @1991084，精确顺序见 §14.3）：
  1. `!proxyArm` → 发 ChannelType canned（proxyArm 则在 DC-open 时发）
  2. `mIsStart && sdkData.gsmToken`（**仅重连**）→ `sendSDPInfo()`（DC authV2）+ `mm(gsmToken, codecType, userId)`
  3. 否则**首连**：DISPATCH → `Mu(packedData, JSON(sdpInfo))`（PackedData，精确编码见 §14.3）；DIRECT：`xY(...)`（WLCGV2 ClientFirstOnline）
- `cmd="启动进度"`（UTF-8 常量），`arg` 枚举：`"准备开始游戏"`(READY) / `"服务端正在处理存档"`(HANDLE_ARCHIVE) / `"游戏开始"`(START_GAME)
- `cmd="踢下线"`（被踢）；`cmd="来自游戏的数据"`（转发给游戏层）
- `NotifyEvent`（arg: `verification_needed` / `login_failed`，Steam 相关）
- `PipeState`、`SendResolutionByClient`、`UploadFileResult`、`BigDataToGameWithKeyResult`

### 8.4 SDPInfo（authV2 JSON，@1989473）

```json
{
  "local":{"v":"5.14.1.260916160035-wlweb-release","os":"h5","t":"h5","dpi":0,
           "decode":<codecType>,"sample_rate":48000,"channel":2,"audio_format":"opus",
           "screen_width":1920,"screen_height":1080},
  "user":{"u":"<userId>","t":"<gsmToken>","p":"<playerId>","r":"<playerRole>"},
  "function":{"tr_file_pb":0,"tr_json_pb":0,"input_ver":[1,2,3],"support_cmd_v2":true},
  "net":{"t":"connected"}
}
```

## 9. 输入协议（bundle @2032xxx / @3212400 / @3395700）

### 9.1 CG.PROTO.OneInputOPData 字段

| field | 名称 | 说明 |
|---|---|---|
| 1 | input_op | 动作（见枚举） |
| 2 | input_state | 状态：DEFAULT=1, DOWN=2, UP=3, VK_UNTOGGLED=4, VK_TOGGLED=5 |
| 3 | value | 数值（滚轮/扳机/摇杆等） |
| 4 | cursor_pos | CoordinatePos{x:1,y:2} |
| 5 | mouse_pos | (同型) |
| 6 | op_device | 设备：KEYBOARD=1, MOUSE=2, XINPUT=3, GAMEPAD=4, TOUCH=16 |
| 7 | xinput_user_index | 手柄序号 |

### 9.2 动作枚举（节选，完整块见 bundle @3211xxx）

- 鼠标：`OP_MOUSE_BUTTON_LEFT=512, MIDDLE=513, RIGHT=514, WHEEL=515, MOV=516, XBUTTON1_BACK=518, XBUTTON2_FORWARD=519`
- 键盘：VK 码 0-510（Windows 虚拟键码体系；特殊：Ctrl 162/163、Shift 160/161、Alt 164/165）
- XInput：`OP_XINPUT_BUTTONS=1024, LEFT_TRIGGER=1025, RIGHT_TRIGGER=1026, THUMB_LX=1027, LY=1028, RX=1029, RY=1030`
- 触摸：`OP_TOUCH=1152`；默认：`OP_INPUT_OP_DEFAULT=0`

### 9.3 键盘映射表（JS `event.code` → VK，bundle @3395956 全文）

`Backquote:192, Backslash:220, Backspace:8, BracketLeft:219, BracketRight:221, Comma:188, Digit0-9:48-57, Enter:13, Equal:187, F1-F12:112-123, Tab:9, KeyA-Z:65-90, Minus:189, Period:190, Quote:222, Semicolon:188, Slash:191, Space:32, Alt:18, Control:17, Shift:16, Meta:91/92, CapsLock:20, ContextMenu:93, Delete:46, End:35, Home:36, Insert:45, NumLock:136, PageUp:33, PageDown:34, Pause:19, ScrollLock:137, ArrowUp/Down/Left/Right:38/40/37/39, Escape:27, PrintScreen:44, Help:47, AudioVolume*:173-175, Numpad0-9:96-105, NumpadAdd:107, NumpadSubtract:109, NumpadMultiply:106, NumpadDivide:111, NumpadDecimal:110, NumpadEnter:13`
修饰键左右分列但取同一 VK：`ControlLeft/ControlRight→17, ShiftLeft/ShiftRight→16, AltLeft/AltRight→18`（此前 162/163/160/161/164/165 的推测已按 bundle 逐字更正）。扩展键按 bundle 原样：`IntlRo→193, IntlYen→165, Semicolon→188`，完整表（含 Numpad/扩展段）逐字保留于 `app/src/endfield/wl_input.cpp`（@3395956）。

### 9.4 发送形态

- 单点输入统一包装（修正）：`S(p(...),"CtrlMsg")` → **`SimpleCmd{cmd:"CtrlMsg", data=CG.PROTO.InputOPData{op_list:[OneInputOPData]}}`**；`Keyboard`/`mouseEvent`/`mousemove`/`gamePadEvent` 等仅是本地日志标签，不是线上字段
- 键盘：`p({device:KEYBOARD, index:0, state:DOWN|UP, action:VK})` tag `Keyboard`
- 鼠标移动：`p({cursorPos, mousePos, device:MOUSE, index:0, action:OP_MOUSE_MOV, state:DEFAULT})` tag `mousemove`
- 鼠标键：同上 `action:OP_MOUSE_BUTTON_*, state:DOWN|UP` tag `mouseEvent`
- 滚轮：同上 `action:OP_MOUSE_WHEEL, value:delta` tag `mouseEvent`
- 手柄：`p({device:XINPUT, index, action:OP_XINPUT_*, value})` tag `gamePadEvent`；全量态用 CtrlMsgV2
- 保活：`p({cursorPos:{x:1,y:1}, device:999, index:0, action:0, value:1})` tag `keepAliveForGame`
- 光标坐标含视频区归一化逻辑（`currentVideoArea`；对称/旋转模式）— 见 @2030xxx，实现时按 16:9 直通处理即可

## 10. 画质/码率（bundle @736100 附近）

- 档位表 `BIT_RATE_LEVEL_MAP`（kbps 区间，数值待二次核对）：low ≈ 1000-4000、medium ≈ 3000-8000、high ≈ 4000-10000(+12000)
- `dispatch.bit_rate = 档位.upperLimit`；`fps:60`
- 运行中可用 `ResetVideoBitrate / autoBitrateAdjust / SetFps` 调整（省流默认走 low）

## 11. 待实测确认清单（实现期逐个验证）

1. JWE 的 alg/enc 具体组合（ECDH-ES+A128KW/A128CBC-HS256 优先猜测）
2. `Mu/xY/mm/pR` 的实际发送通道与报文（ws-proxy vs DC）— 游戏启动握手关键
3. `sdk_msg` 完整字段与 `codec_type` 取值集合（18=H264 / 21=? / 25=AV1）
4. `ClientOnline` 的发送时机/是否需要（Direct 路径）
5. 初次连接 vs 重连（mIsStart 区分）行为差异
6. 排队轮询间隔与 UI 呈现对应关系（10s；节点切换 21003）
7. `X-Client-Device-Id`/FingerprintJS 是否被服务端强校验（expected: 否）

## 12. 接入实现方案（Switch 端）

新增独立模块（与 mihoyo 表分离，发行商/技术栈不同）：

```
app/src/endfield/
  hg_api.{hpp,cpp}       // HTTP + 签名(HMAC) + exchange/dispatch/poll/cancel/node_list
  hg_login.{hpp,cpp}     // 原生通行证扫码登录（gen_scan/login → scan_status → token_by_scan_code）
  hg_login_http.cpp      // 扫码登录 HTTP（as.hypergryph.com，无需 X-Signature）
  jwe.cpp                // ECDH P-256 + JWE compact 解密 (mbedtls: ECC/KW/AES-CBC-HMAC/GCM 备选)
  vlink_session.{hpp,cpp}// ws-proxy 信令 + libpeer offerer + 候选改写 + 心跳
  wl_proto.{hpp,cpp}     // SimpleCmd / OneInputOPData 最小 protobuf 编解码（手写，无需 protoc）
  wl_input.{hpp,cpp}     // 键盘 VK 表/鼠标/手柄映射 + 保活帧
  endfield_ui.cpp        // 第四个游戏入口 + 排队/状态 UI
```

关键依赖变更：libpeer 需支持「客户端创建 DataChannel（DCEP OPEN + 发送）」与 offerer 模式（媒体管线复用现有 H264/Opus 收流）。

验收路径：默认最低画质；评测在 yuzu（用户执行）→ 真机。测试消耗账号云时长/排队，需用户决定时间点。

### 12.1 已落地（0.2.17，未提交）

- `hg_api`（含 `_http`）/`jwe`/`endfield_store`/`wl_proto`/`wl_input`/`vlink_proto`/`vlink_session` 已实现；纯协议层主机自测 44+68+62 项 0 失败（sdk_msg/PT/BH 改写、VK 表、输入编码逐字节、签名/时钟、JWE 向量），Switch 构建通过。
- MVP 凭证录入（替代 `hg_login` QR 前的过渡）：官方 hg_token 原样存为应用目录下 `endfield-login.txt` → 启动器终末地面板「导入登录凭证」→ `store::Save` 加密入库并删除明文文件。（面板实现落在 `cloudgenshin_tab.cpp`，未单开 `endfield_ui.cpp`。）
- 第四个游戏入口 + 终末地面板（状态/导入/清除/开始）已上线；「开始」暂被门控——串流视图未接通，避免白耗云时长。
- 剩余：启动编排（vault → exchange → dispatch/poll → `VlinkSession`）与串流视图（视频渲染 + `wl_input` 输入桥）。

### 12.2 原生扫码登录（0.2.18，官方 APK 取证）

证据：官方 Android 客户端 `EndfieldCloud-1.1.6.0.apk`（`android.official.cloud`，HGSDK）反编译
`com.hypergryph.platform.hgsdk.http.business.NewApi`、`...login.fragment.HGSDKQrCodeFragment`、
`...contants.SDKConst.ScanLoginStatusCode`。协议与线上实测一致。

```
POST https://as.hypergryph.com/general/v1/gen_scan/login
     {"appCode":"dd7b852d5f1dd9da","enableRememberLogin":true}
  -> data:{scanId, scanUrl, enableScanAppList[]}
     scanUrl 即二维码内容：hypergryph://scan_login?scanId=<scanId>

GET  https://as.hypergryph.com/general/v1/scan_status?scanId=<scanId>   （官方 2 s 轮询）
  -> status 0 + data.scanCode   已确认，可换 token
     status 100                尚未扫码
     status 101                已扫码，等待手机确认
     status 102                二维码过期

POST https://as.hypergryph.com/user/auth/v1/token_by_scan_code
     {"appCode":"dd7b852d5f1dd9da","from":0,"scanCode":"<scanCode>"}
  -> data:{token, deviceToken, rememberLogin}
     token 即 hg_token（写入现有 vault）；deviceToken 在云端 Web 路径不使用，不落盘。
     实测 data 中不含 uid（0.2.22 yuzu 扫码：Save stage 5 = uid 为空）。

历史 uid 方案（0.2.23，2026-10-24 起不再作为云登录的前提）：
  POST https://as.hypergryph.com/user/oauth2/v2/grant
       {"token":"<hg_token>","appCode":"be36d44aa36bfb5b","type":1} -> data:{token}
  GET  https://binding-api-account-prod.hypergryph.com/account/binding/v1/binding_list
       ?token=<grant>&appCode=4df8f5a7c2ad711b497a     （SDK u8AppCodeMap.endfield）
    -> data:{list:[{appCode, bindingList:[{uid,isDefault,isOfficial,...}]}]}
       选条规则同 SDK：isDefault → 首个 isOfficial → 首个条目；随后
  POST .../account/binding/v1/u8_token_by_uid {"token":"<grant>","uid":"<uid>"} -> data:{token}
  实现：`hg_api.cpp FetchBindingUid`（扫码后 swap 无 uid 时先解析再存 vault v2）。
```

- `appCode` 为官方常量，来自随包远程配置：
  `GET https://game-config.hypergryph.com/api/remote_config/105/prod/default/default/app_config_endfield`
  → `channel.appCode = dd7b852d5f1dd9da`（与 Web 客户端 oauth appCode 相同）。
- 线上实测（0.2.18 开发期）：创建返回 `status=0`，data 仅含上述三键；未扫码轮询返回 `status=100`。
  未记录任何 scanId/scanUrl/token 内容。
- Passport 接口不需要云端 `X-Signature`；仅 `Content-Type: application/json` 即被接受，未伪造设备头。
- 实现：`hg_login.{hpp,cpp}` 纯协议（请求体/信封/状态机/响应解析）+ `hg_login_http.cpp`（`opennow::HttpClient`）；
  主机自测 15 项 0 失败。面板在 `cloudgenshin_tab.cpp`：第四个入口选中后自动取码并渲染，
  2 s 轮询；成功后 `store::Save(hg_token)` 并清空二维码；可手动刷新/清除。
- 旧的手工 `endfield-login.txt` 导入函数保留为兜底，但 UI 不再暴露，不作为目标流程。
- 0.2.18 yuzu 实测（不扫码、不消耗云时长）：第四个入口启动后成功取码并渲染（仅做 300×300
  区域的像素统计，未读取/输出二维码内容），状态停在「等待扫码」，30–35 s 内无网络/解析失败，
  进程存活；真实 Switch 上的扫码确认与 token 落盘仍待用户执行。
- 线程生命周期：libnx 上 `std::thread::detach()` 实测失败，临时 thread 析构时仍 joinable，
  触发 `std::terminate`，应用在取码前退出。已改为成员线程，在刷新/清除/切游戏/离开/析构时
  先置 `stop` 再 join（HTTP 取消回调使 join 快速返回），不依赖 detach。

## 13. 参考偏移（index.cda000.js 关键位置）

| 主题 | 偏移 |
|---|---|
| 环境/常量表 | @733541 / @734166 |
| 签名函数 A / HMAC k / 辅助 | @1792613 / @1791600 |
| exchange（t7）与 23023 模块导出 | @1827359 / @1786331 |
| 会话 API（t0/t2/t3/t4…） | @1827620 附近 |
| exchange 调用点（hg_token,u8_token） | @1283802 |
| 连接流程（offer/answer） | @1919000-@1925500 |
| 模块 6868（状态位/类） | @1935345 |
| WeLink 默认 configs | @1963900 |
| startGame / preConnectToGS / generateSocketUrl | @1973xxx-@2015xxx |
| getSDPInfo/messageDetail 切换 | @1989473 / @1990737 |
| 传输/输入发送（模块 6718） | @2026160-@2036000 |
| 键盘表（模块 7798）/ KEYCODE 表 | @3395956 / @1907941 |
| proto 定义（OneInputOPData 等） | @3212400-@3221229 |
| 会话/信令（模块 2014，protoVersion/DC-open/ws-proxy） | @1913544-@2017xxx |
| 浏览器路径信令（Yr/I 函数、候选改写、READY 间隔） | @1917xxx-@1920100 |
| 候选改写模块 9399（BH/KH）、平台枚举 4042（ag/sy） | @1921892 / @3385105 |
| 首连/重连握手调用点（handleStartGame/preConnectToGS） | @2010800-@2014000 |
| 画质档位模块 62155 | @737066 |
| sdk_msg/排队对象解析（模块 @81xxxx） | @812728 |

## 14. 追加取证（2026-10-17 二轮：修正 + 新事实）

> 全部来自 `index.cda000.js` 本轮抽取。**我们复刻的目标路径 = rtcType=WEBRTC 的浏览器路径**（libpeer 替代 RTCPeerConnection）：`/ws-proxy` 客户端 offer + `welink` DC + CtrlMsg 输入 + 心跳。WL_RTC/WGS(WASM) 路径不复刻（输入编码在 WASM 内，JS 不可见）。

### 14.1 修正

1. **首连 ≠ 重连握手**：`protoVersion` 后，`mIsStart&&gsmToken`（重连）才发 `authV2`+`mm`；**首连**发 `PackedData`（DISPATCH）或 `ClientOnline`（DIRECT）。
2. 心跳两条线（2s canned / 1s SDKHeartBeat，见 §8.2 修正与 §14.4）。
3. 单点输入真实形态 = `SimpleCmd{cmd:"CtrlMsg", data=InputOPData}`（§9.4 已修正）。
4. rtcType 选择逻辑（@3449060）：WebRTC 支持 H265→WEBRTC；WLRTC 支持 H265→WL_RTC；WebRTC 支持 H264→**WEBRTC**；否则 WLRTC。普通浏览器（含我们的复刻）走 WEBRTC 路径。
5. 旧 §8.3 待实测项（Mu/xY/mm 通道）已定案，见 §14.2/§14.3。

### 14.2 信令细节（浏览器路径）

- gs 地址：`ws://<currentIp>:<webRtcPort>`（proxy-arm 追加 `/webrtc-proxy`，@2013990；我们不用）。currentIp/webRtcPort 来自 sdk_msg（`gsPort`→webRtcPort 亦见 @1977180）。
- ws-proxy（DISPATCH，@1919900）：
  1. `new WebSocket(signalDomain+"/ws-proxy")`；onopen 发 `{"gsUrl":"ws://<ip>:<port>"}`
  2. 收到 `code==0 && message 含 "connected to gs"` → 非 proxy-arm 直接发 `JSON.stringify(offer)`（`{"type":"offer","sdp":...}`，浏览器 RTCSessionDescription）
  3. 收到 `{code:0, answer:{type:"answer", sdp}}` → `E(answer)` 候选改写 → `setRemoteDescription` → **成功后每 1s 发送字符串 `"READY"`**（`C=()=>{LU("READY","requestStartGame")}`，@1917xxx；DC 上为文本消息，非 protobuf）→ 停止条件未确认（随会话结束清除）
- 候选改写 `BH(sdp,currentIp,"a=candidate",webRtcPort)`（模块 9399 @1921914，非 proxy-arm）：对每条含 `a=candidate` 的行：IPv4→currentIp；该行 IP 之后、`" typ host"` 之前的端口段→webRtcPort（无 ` typ host` 时取 IP 后到行尾前一字符）。净效果：**answer 的全部候选重定向到 relay 端点 `currentIp:webRtcPort`**。
  - proxy-arm 用 KH：整段替换为固定 srflx 候选（不用）。
- DIRECT（legacy）：ws 直连 pod，pod 先发 `{"type":"offer"}` → 客户端 answer → `{"type":"answer_ice"}` trickle（不用）。

### 14.3 首连 DC 握手（模块 2014/6718）

`protoVersion` 处理顺序（gate 一次）：
1. `oC(true)`（本地已握手旗标）
2. microTerminalInfo 存在 → `pR(JSON)`（Microterminal）
3. `yQ()` → `SimpleCmd{cmd:"LocalIm", arg:"1"}`（tag sendLocalIme）
4. 非 proxyArm → canned **ChannelType**（§8.2 字节）
5. 分支：重连→`authV2`+`mm`；首连 DISPATCH→`Mu(packedData, JSON(getSDPInfo()))`；首连 DIRECT→`xY(...)`
6. `tw` 条件（含义未确认）→ `SetDPI(480)`（`$e(configs.dpi||480)`）
7. `tv.startInterval()`：1s gameData 重发 + 20s 统计（与心跳无关）

- **Mu(PackedData)**（模块 6718 `g`）：`SimpleCmd{cmd:"PackedData", arg:JSON(sdpInfo), data:btoa(encodeURI(packedData))}`；tag `startGameDispatch`。packedData 为 **sdk_msg 对象字段**（与 auth/verify/assignResultParam 同在；@2011700：`e.auth→e.packedData→e.verify` 齐全才 preConnectToGS，缺→INIT_PARAM_NOT_EXIST）。
- **mm(auth)**：`SimpleCmd{cmd:"auth", arg:gsmToken, arg1:String(codecType), data:utf8(userId)}`（`sy`=TextEncoder，@3385105 模块 4042；tag `auth`）。**仅重连**。
- **xY(ClientOnline/Direct)**：WLCGV2 `ClientFirstOnline`{gsm_id, device_uuid, token, client_ver, video{width,height,codec_type,bit_rate,fps}, gsm_token, serial_id, save_id, game_id_str, game_mode, instance_id, account_id, cmd_line, cursor?, extparam, gamepath, cfgpath, sdkInfo}；tag `Direct`。我们不用。
- **SimpleCmd tag→字段映射**（模块 6718 `S` 全文）：data={ClientOnline,CtrlMsg,ChannelType}；arg={InputString,ClipBoard,ResetVideoBitrate,autoBitrateAdjust,BusinessDataConfirm,StateSync}；SendDataToGame→arg=data,arg1=index；DataToGameWithKey→data=data,arg=index,arg1=key；SetVideoArea→arg,arg1；SetFps/SetCursorMode→arg；auth→arg,arg1,data；authV2→arg。

### 14.4 精确 canned 常量与报文

- **WelinkHeartBeat**（DC 打开后每 2s；字段1=3 汉字"测延时"9 字节，字段2=13 位数字串）：
  `[0x0A,0x09,0xE6,0xB5,0x8B,0xE5,0xBB,0xB6,0xE6,0x97,0xB6,0x12,0x0D,49,54,55,56,48,56,52,50,51,55,54,54,55,0x22,0x01,52]`
  （实现时**直接使用本字节数组**，不要手抄字符串）
- **exitGame**（用户主动退出会话时发送；字段1="用户主动退出"）：
  `[0x0A,0x12,0xE7,0x94,0xA8,0xE6,0x88,0xB7,0xE4,0xB8,0xBB,0xE5,0x8A,0xA8,0xE9,0x80,0x80,0xE5,0x87,0xBA]`
- **keepAlive**（非 WL 路径）：`p({cursorPos:{x:1,y:1}, device:999, index:0, action:0, value:1})` → CtrlMsg 包装；tag `keepAliveForGame`
- **pause/play**：`changeAVC` arg="0"/"1"（arg1="0"/"1"，pause+t 时 arg1="1"）
- **SetDPI**：`SimpleCmd{cmd:"SetDPI", arg:"480"}`
- **DeviceData(ime)**：`SimpleCmd{cmd:"DeviceData", arg:"ime", arg1:<height>}`

### 14.5 dispatch 三字段定案

- **biz_data**（模块 6718 `h` = `getBizData()`）：
  `JSON.stringify({btype:"", os:<ag()>, osVer:<UA 解析>, clientVer:"5.14.1.260916160035-wlweb-release", osCategory:"H5", isOneLine:1, extSDK:JSON.stringify({certHash:true}), ping:[{nodeId,result}]?})`
  - `ag()`（模块 4042）：UA→"ANDROID"/"IPAD"/"IOS"/"MAC"/"WINDOWS"/"LINUX"（识别失败为 ""）。
  - osVer：Firefox=UA.split(")")[1]；Chrome/Safari/Edge=UA.split(")")[2]；否则整串。
  - ping：仅有节点测速结果时带（web 端）。
  - Switch 端取值策略：os 做成常量（默认 "WINDOWS"，匹配 pod Windows 键码/光标语义；备选 "" 或 "H5"），实测后定。
- **cmd_line**（模块 96042 `NB` @1830233）：服务端 gameConfig.cmdLine 模板 + `replace(/%[dsb]/g, 依次 [cloudPlatform, "WxH", "leftxtopxrightxbottom", true])`；staging 模板 = `-cloud_platform=%d -cloud_display_resolution=%s -cloud_safe_area=%s -cloud_is_web=%b`。cloudPlatform 取 launchEnvironment（web 值待定，先用同 web）。
- **画质档位**（模块 62155 @737066）：标准(≤1080p)：low{4000,1000} / medium{8000,3000} / high{12000,4000}；1080P+：low{4000,1000} / medium{8000,3000} / high{10000,4000}。`bit_rate=upperLimit`(kbps)；`fps:60`；`codec_type` 初值 **18**。
- pay_type：`1`=普通（我们只用）;`2`=付费快速（resolveQueuePay 仅在钱包/体验卡条件下出现）。

### 14.6 getSDPInfo 补充（@1990060）

screen_width/height 取视频容器 clientWidth/Height，fallback 1920/1080；net.t=connection.type||effectiveType||"connected"；dpi:0；os=ag()||"h5"。

### 14.7 WGS/WL_RTC 路径（记录，不复刻）

`D3(ip,port)`（模块 5708）→ `d.open(cb)` → `h.connect(ip,port, hashGWHost+"/cert_hash/"+ip+"/"+port, {auth:{token:gsmToken,uid:userId,"transport-protocol":1}, game_info:{}}, cert_hash)`；输入经 `nP.*`→worker→WASM；回调 onconnected/onstat/ondata/onclose/onfirstframe/setanswersuccess。判别：`rtcType===WL_RTC && gsInfo.input_ver>=3 && wlRtcProto`。

### 14.8 待实测（更新）

1. 首连握手后 pod 的"启动进度"序列；1s `"READY"` 的停止条件
2. answer 候选改写后 libpeer ICE 能否经 relay 端点连通（当前 relay 假设）
3. `SetDPI(480)` 是否必需 / 是否影响输入
4. 首连不发 authV2 是否被 pod 接受（如被拒：加 flag 首连同时也发）
5. `biz_data.os` 取值对 pod 行为的影响（先 "WINDOWS"）
6. sdk_msg 完整字段清单（运行时安全日志确认 auth/verify/packedData/assignResultParam 实际存在）
### 14.9 Transport facts verified on 2026-10-24 (Switch build)

- **DTLS role**: the pod answers =setup:passive, i.e. it waits for the client flight. The offer is therefore created with peer_connection_set_offer_dtls_client(peer,1), which emits =setup:active *and* configures mbedTLS in client mode. Changing only the SDP text left both sides waiting for a ClientHello.
- **Answer fingerprint offset**: =fingerprint:sha-256 AA:BB:... is 22 bytes up to and including the trailing space, so the digest starts at offset 26. Parsing at 22 made every real handshake fail with Remote certificate fingerprint mismatch. The previous host check passed only because its fixture never reached that path.
- **opus fmtp**: the official player rewrites the received answer with ;stereo=1 (@1915604, S()). Our offer now advertises =fmtp:111 stereo=1 so the pod negotiates the stereo stream it actually sends.
- **Startup writes**: LocalIm -> ChannelType (non proxy-ARM) -> PackedData -> SetDPI (ARM only) are replayed with identical bytes until accepted, capped at 15 s; each attempt's result is recorded (ENDFIELD_STARTUP_SEND_ATTEMPTS/FAILURES) instead of being dropped silently.
- **Post-first-frame settings**: SetFps(30) / intercept_app retry on a 500 ms backoff for at most 20 attempts; if they still fail the UI reports STREAM_SETTINGS_NOT_ACCEPTED rather than waiting for the generic first-frame timeout.
- **Allocation release**: the dispatch allocation is owned by an RAII guard, so queue poll failures, a malformed sdk_msg, transport exceptions and a cancel racing the poll all release the pod exactly once.
### 14.10 Session teardown + host verification (2026-10-24)

- **ExitHold latch**: ExitHold::update used to return true on every call once the combo had been held 800 ms. The stream views poll the pad on every draw, so a held + and - re-requested the activity pop on every frame. It now fires once per hold and re-arms only after the combo is released. (mihoyo/stream_diagnostics_policy.hpp, covered by tests/mihoyo_stream_diagnostics_policy_test.cpp)
- **Worker completion ordering**: both stream views now write STREAM_WORKER_FINISHED and only then set the finished flag. Previously the flag became visible while the worker was still logging, so the UI could destroy the view (whose destructor joins that thread) during a live log write.
- **Pop outside draw**: the activity pop is deferred through brls::sync and guarded by an atomic re-entry latch, so borealis can never run the hide animation (and the deletion callback) from inside the view's own draw().
- **Unattended end**: an autotest session now exports the log and then quits the process after the pop, instead of idling on the start page. A normal session still returns to the game list.
- **Host verification is real now**: winget WinLibs GCC 16.2 gives a native compiler, so scripts/run-host-libpeer-tests.sh runs the libpeer checks under ctest (dtls_handshake, peer_dtls_setup, peer_dtls_receive, peer_dtls_loop, agent_socket_poll, ports_host_addr) and scripts/run-host-policy-tests.sh covers the header-only policies.

### 14.11 黑屏、游戏交互及凭据类型复核（2026-10-24）

- DISPATCH 首连只发 `PackedData`；`authV2 + auth` 是重连分支。交接实验中前者授权成功，后者无媒体，同时发送两种启动消息则被踢下线。删除临时启动模式开关，避免再次混用。
- `Mu` 调用 `btoa(encodeURI(packedData))` 后交给 jspb 的 **bytes** setter；writer 会解码 base64。因此线上 bytes 是 `encodeURI(packedData)`，不能再包一层 base64。保留 `+ / = & ,` 等 encodeURI 的未转义字符。
- 客户端固定在自建 SID 0 写入，pod 打开的 SID 1 只接收。即使远端 OPEN 先到，也不能把写通道或心跳暂时转到 SID 1。`protoVersion` 到达即停止 READY；answer 应用后的信令 WebSocket 关闭不再中止媒体。
- `SDKHeartBeat` 从首个解码帧开始。每个 `BusinessData` 必须回 `BusinessDataConfirm{arg:id}`。`OnCursorData` 的 show/hide 是鼠标可见性，不能据此推断已进入游戏。
- 交接记录里的动态音频和近黑 SDR 帧只能证明音频仍活跃，不能排除 RTP 接收问题。本轮补充视频专用计数后发现：139 秒基线有 571 次序号缺口、574 个损坏 access unit，解码停在 40 帧；“云端只发送黑图”的早期推断不成立。
- 接收缓冲曾直接使用发送 MTU 1300，装不下远端完整的以太网大小 SRTP 包。现在接收容量为 2048，发送 MTU 仍是 1300；Endfield 每轮最多接收 64 包或 2 ms，空闲即退出，避免固定每 2 ms 只取一个包造成积压。需要这个窄范围 libpeer 补丁来修复完整数据包接收；不改变 TLS/SRTP 校验。
- `receive-capacity-20261024-12` 实测 104 秒：接收错误 0、视频 RTP 31241 包、拼帧缺口/损坏各 3、解码 2518 帧、解码错误 0，后段接近 30 fps。彩色帧已能识别终末地启动页和登录失败弹窗；这是启动画面证据，**不是已进入游玩的证明**。129 个解码侧丢弃主要出现在启动重同步期间，不等于全程零丢帧。
- 首帧还要走网页 **gameManager** 的 `sendPipelineConnected()`：`DataToGameWithKey{arg:index,arg1:key,data:UTF8(JSON)}`。回应 JSON 包含 data/code/eventId/isGlobalEvent/key/message；PipelineConnected 是全局事件。单独实现 `GameBridge`，有界保存未确认回复，收到 BusinessDataConfirm 删除，失败有间隔重试并于 15 秒到期。
- pod 的 keyed BusinessData.data 是**内层 SimpleCmd protobuf**（模块 6718 的 Qm），字段 5 以 `wl` 开头，内层 cmd 是业务 key、data 是 UTF-8 JSON。它不是 MsgPack。已实现 RequestLaunchInfo、RequestLogin、GetSafeArea、GetGamepad；SetU8Token 不可覆盖游戏授权码。实测收到 LaunchInfo/Gamepad/Login 请求及前三条回复确认。
- **关键凭据纠正**：网页模块 61522 `Q/B4` 返回变量 `q`，它来自 OAuth CLIENT grant 的 `data.code`；U8 token 是另一个变量 `c`，用于 cloud exchange。游戏 `loginInfo.msg{code,isSuc,type:1}` 要的是前者，不能把 U8 token 作为 code 发送。RequestLogin 需刷新授权码；新实现通过后台 HTTP 任务刷新，RTC 接收线程继续轮询。
- 保存账号的 OTHER grant 与 U8 authByHG 成功，但 CLIENT grant 当前实际返回 **109（官方设备校验）**。这不表示旧 passport 失效，不能清账号、强制重扫或尝试绕过验证。新版本明确显示“游戏登录需要官方设备确认，已保留账号”，缺少授权时发送失败状态而非伪造成功。
- QR 交换原本丢掉了官方返回的 deviceToken。新 v3 加密存档保存该设备证明，仍读取旧 v1/v2；本轮没有改写用户的旧存档。授权码本身不落盘。
- 自动输入恢复为单独 `.click` 精确标记许可的一次入口点击；没有标记不点击。移除 Alt+Tab、Alt+Enter、静音、Alt+F4 和盲扫探针；自动测试上限回到 120 秒。实测 11/12 轮无输入标记，没有自动按键。
- 安全日志不再记录未解析数据的可逆 hex。被服务器踢下线属于失败；渲染提交仍不宣称展示或游玩成功。21002 按官方含义显示上次存档处理中，最多 3 次、每次 20 秒的可取消等待。

### 14.12 首帧字段、排队状态与授权时机复核（2026-10-25）

以下偏移均针对本地官方 `index.cda000.js`，不包含运行时协议正文或凭据。

- `ensureResolutionSyncedAfterConnect` @823724 无条件调用 `A.xd(width,height)`；模块 65908 @1775548 转至 `setGameResolution`，模块 6718 @2018108 编码 `SimpleCmd{cmd:"SetGameResolution",arg:String(width),arg1:String(height)}`。只有动态尺寸与派发不同才另发 keyed `ResolutionChange`；不能把后者误当成固定 1280×720 启动必需消息。
- `eK` @796300 的 deviceProperties 是 getPresetProperties JSON 与渠道属性合并后的 JSON 字符串；预设属性白名单 @102900 包含 SDK、设备、OS/UA、浏览器、页面及渠道属性，缺省字段由官方 SDK 自己过滤。原生补充已知属性和随机 app 标识，未伪造服务端 sdid。早期 Native C++ / 项目版本已改为与协议匹配的 Web JS / 4.6.1 / 1；APK 静态属性另属 AndroidSdk profile。设备属性是否是原加载停滞原因尚未证实。
- `launchEnvironment` @1777393：鼠标/桌面模式 `cloudPlatform=2,envType="pc"`。它和 eK 的 `platform=7` 分属不同枚举，原先派发命令行用 2 是正确的，不能为了数值相同改成 7。
- gameManager dispatch @813677 只在 `dispatchResult.status===2` 调用 startGame；updateDispatchResult @825020 只存储 sdkMsg，不以其字符串真值判断分配。原生现按 status=2 判断，避免 `sdk_msg:{}` / `"{}"` 在排队中提前结束等待。
- gameManager.startGame @816500 附近在取 sdkMsg 和连接前执行 `await K.cq()`。模块 61522 的 ex / checkAndUpdateToken 重新 CLIENT grant、tokenByOauth2Code、校验已接受协议、exchange，更新缓存 q；eH 读取 q。APK 后续取证确认游戏刷新仅需独立 CLIENT grant；SavedGameAuthorization 已按 APK 分离，不在游戏登录请求中再创建 U8/cloud 会话，RequestLogin 仍异步刷新。两者差异及实测见下节补充文档。
- CloudBrowserEvent 的 tv @802500 除 Open/OpenBrowser 外不做打开动作，但每个方法均回应 OnBrowserOpenResult code0。原生非打开方法与之对齐；真实网页请求明确失败，不表示已访问或已接受网页条款。
- 官方鼠标按钮/滚轮会带最后位置的 cursorPos、mousePos；滚轮还设置 DEFAULT=1。原生与黄金 protobuf 字节检查已对齐。修正点击字段后启动画面仍未推进，因此不把输入修复声称为已解决加载。
- 用户已自行完成设备确认；v3 加密证明在重启进程后可复用。109 未完成是 §14.11 的历史状态。只读 agreement_version 结果为授权且最新，没有自动接受协议。

第 24 轮无自动点击，普通队列第 7 位推进至分配；首帧分辨率消息已发送，业务确认累计 5。尚无账号/角色进入事件或实际游戏帧。不能把 `PipeState=1`、30 fps 动画、OnCursorData 或角色离开事件当成游玩成功。

### 14.13 虚拟手柄设备接入（2026-10-25）

- 官方模块 6718 的 R @2034130 附近在手柄连接/断开时发送 `CtrlMsgV2`，并不是仅发送按钮值或向 GameBridge 报告设备。`WLCGV2.InputOPData.op_list(1)` 中的 OneInputOPData 使用 `op_device(1)=3`、`state(11)=Connect`；Connect 写入 `connectstate(1):bool,userindex(2):int32,padtype(3):int32`。来源 serializeBinaryToWriter @3234526 / @3288650。
- `WLCGV2.OneInputOPData.GamePadType.GAMEPADTYPE_XBOX=0` @3237837，与 UI 元数据 Xbox=11 是不同枚举；不能混用。连接、断开和槽位 0 都有显式零字段，不能当作 proto3 默认值省略。
- 原生现先确认本地接入消息被接受，再发送按钮、扳机和摇杆值；失败不更新缓存；断开/重新接入会重置已交付控制值，确保后续完整状态重新发送。正常退出尽力发中立状态后断开。
- 第 27 轮 `ENDFIELD_GAMEPAD_CONNECTION_SENT=1`，此后仍为启动动画。说明接入缺失已修复，但不能据此断言加载停滞的唯一原因已找到。
- 回查此前未识别的安全命令哈希：除 BusinessData/Confirm 外，都是本客户端 WEILINK_* 诊断回调与官方“回测延时”。未发现一个可据此认定为漏处理登录请求的新命令。

### 14.14 APK 取证与当前边界（2026-10-25）

用户给出下载目录的 EndfieldCloud-1-1.6.0.apk 后，已直接反编译主程序与 wl_game 插件，字段/回调/时序/生产配置及 32–37 轮结果见 [APK 协议对照](endfield-apk-port-2026-10-25.md)。AndroidSdk 的小写 onRequestLaunchInfo、OnGetGamepad、OnCloudBrowserEvent 不可混入使用网页 gameManager 的 WebRTC profile；两套协议都有测试，VlinkSession 明确选择 WebRtc。

APK 明确 PipelineConnected 来自 PipeState=1，与视频首帧独立；现按此触发，SDKHeartBeat 仍在首帧后开始。APK 的游戏 CLIENT grant 刷新与 U8/cloud 分配凭据分离；不更改保存账号及设备证明。主 APK 的 126 个局部反编译错误仍需保留记录，不能声称完整还原。

输入协商修复已收到实际 input_ver=3，Xbox 全状态使用 V2；ginput_level=1 是另一个字段。实际 Xbox A 与鼠标入口点击均已发送后仍停片头。第 35 轮持续 587 秒，解码 16432 帧、错误 0，但没有账号/角色进入。因此不把此结果报告为已正常游玩。
