# 终末地扫码后账号信息请求修复（2026-10-18）

基线：现有 0.2.23 代码，以及本地安全日志
`CloudGenshin-Debug/reports/endfield-relogin-0222/cloudgenshin-safe-final.log`。
日志在取码后记录 `ENDFIELD_LOGIN_UID_FAILED code=400`。该版本把 HTTP 状态
与本地错误码合并，无法从这条旧日志确定具体是 grant 还是 binding_list 返回 400。
二维码生成、扫码确认、凭据交换不是此条日志所指的失败点。

## 已修改

- 依据本地官方 `reference/web/endfield/hg-account-web-sdk.min.js`，模块 7142
  的设备头随二维码创建/轮询/凭据交换、OAuth grant、绑定信息和 u8 票据请求发送。
  UA 与设备头采用一致的桌面 Web 兼容值；设备 id 是运行期随机 UUID，线程间一致，
  不读取硬件序列号，不写入日志，也不使用账号信息生成。
- binding_list 查询中的 grant token 逐字节进行 URI 百分号编码，避免 `+ / = & ? #`
  等字符破坏查询参数。
- 账号解析要求明确的整数 status/code 和 data，支持 data 为 JSON 字符串。
  缺少状态、数值类型 token、错误信封及无效 uid 不再被当作成功。
- game-role uid 从 Endfield binding_list 取得，不把 passport/open-id 当成游戏角色 uid。
  角色选择按 SDK 的第一 default、否则最后 official、否则第一条有效角色。
- FetchBindingUid 接受取消标志，传到 grant 和 binding HTTP 请求；取消后不再进入保存。
- 安全事件细分为 `ENDFIELD_LOGIN_UID_FETCH`、`ENDFIELD_ACCOUNT_GRANT_FAILED`、
  `ENDFIELD_BINDING_LIST_FAILED`。`code=` 保留本地/服务错误码，`http=` 单独记录 HTTP。
  不记录票据、二维码、账号 id、请求 URL、响应正文或设备 id。

## 离线验证

`scripts/test-mihoyo-host.ps1 -TestNames endfield_account_policy,endfield_account_http,mihoyo_ui_language`

HTTP 测试替换网络传输，不访问服务：验证两阶段请求、URI 编码、共享设备头、
授权/绑定各自的 HTTP 400、服务数字错误、无效响应、成功返回及取消指针传递。
测试依赖真实协议/加密实现链接，但不含真实凭据。另执行 devkitPro 原生 Switch 构建。

## 尚待验证

这些修复补齐官方 SDK 请求差异和已确认的取消/编码问题，尚未证明解决真实账号的 400。
本轮未启动模拟器，未读取登录 vault/key，未用真实账号联网，未排队或消耗云时长。
下一次手机确认后，成功应走到 `ENDFIELD_LOGIN_OK` 并加密保存。
若失败，只需安全日志中新增的失败事件与 `code/http`；不要复制凭据或二维码内容。
若仍是 grant HTTP 400，应继续对照通行证 token 的授权范围与官方账号 API 要求，
而不是反复调整二维码渲染或跳过账号校验。

版本沿用另一任务现有的 0.2.23，不覆盖此前输出目录；交付单独命名为 account-fix。
