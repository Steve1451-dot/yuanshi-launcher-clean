# 0.2.44：SDK 登录字段保留与有限保活

> 历史版本记录。35-4301 已在 0.2.46 通过旧 vault 的 `account_type` 恢复修复，
> 并已实机进入游戏验证；最终结论见 `docs/fix-35-4301-2026-10-21.md`。

基于 0.2.43 / 本地提交 `1aef982`。这不是 passport 登录改造完成版，
也不表示已经修复原神 `35-4301`。本轮不启动真实云会话、不读用户的 key/vault。

## 本轮修改

- Panda Combo QR 与 Account → Combo 交换均保留官方返回的 `combo_id`、
  `account_type`、`heartbeat`、`asterisk_name`，展开官方 `data` JSON 对象。
- `combo_id` 的整数/字符串、`heartbeat` 的布尔类型保持原样，不做有损数字转换。
- SDK 附加字段从 QR 解析，经 Authorization、加密 vault、串流视图送入
  `login_login`；`login_get_asterisk_name` 也能返回已取得的脱敏昵称。
- 附加数据最大 4096 字节、对象/数组最多 64 项、嵌套最多 8 层；
  账号、渠道、设备与令牌由已校验的本机上下文提供，附加字段不能覆盖。
- vault 保持可选 v2 扩展 `sdk_login_data`，无需不兼容升级。v1、旧 v2
  缺少附加数据仍可读取；不删除/改写旧登录以强制重新扫码。
- 安全日志只记录 `GAME_SDK_LOGIN_*_PRESENT` 的 0/1，不记录令牌/昵称/JSON。
- 游戏请求的 `cloud_keep_alive.duration` 按分钟预算处理：立即刷新，之后每
  60 秒一次，有限次数后停止。次数按官方公式
  `ceil(duration - idle_timeout_minutes + 1)`；重启替换旧定时器，关闭时刷新一次。
- 尚未取得官方 boxConfig 默认时长时，只刷新一次并记录
  `GAME_SDK_KEEP_ALIVE_DEFAULT_UNAVAILABLE`，不猜默认值、不无限保活。
  当前上下文未接入官方空闲阈值，使用 0；非有限数、负数或超过本机安全上限
  1440 分钟的请求不启用定时器。因此这部分仍不是完整的官方设置同步。
- 周期性空闲手柄报告、设备列表通知不再触发“用户活动”；真实按键、摇杆
  超出活动死区、松开操作仍可刷新。输入数据包本身不变。

## 验证方法

`scripts/test-mihoyo-host.ps1` 增加 `mihoyo_sdk_bridge`，链接真实 Mbed TLS
主机加密库，不依赖登录账号或网络。

覆盖：加解密、回包包装、数值/布尔/中文字段、旧登录、无效附加字段、凭据不
出现在诊断文本中、即时/分钟级/停止/缺默认时长的保活、上报失败后重试，
以及空闲手柄不触发保活。另跑扫码、交换、存档、Launch、RTC、输入、语言回归。

Switch 验证必须使用 devkitA64 完整构建。NRO 构建成功也不代表游戏验证成功。

## 下一次实机重点

查看以下事件（只需安全日志，不要发 key/vault 内容）：

- `GAME_SDK_LOGIN_CHANNEL_TOKEN_PRESENT`：网页账号令牌是否存在。
- `GAME_SDK_LOGIN_METADATA_PRESENT`：是否拿到可传递的 SDK 附加字段。
- `GAME_SDK_LOGIN_ACCOUNT_TYPE_PRESENT` / `GAME_SDK_LOGIN_COMBO_ID_PRESENT`。

旧 vault 不会凭空产生之前未保存的数据；没有附加字段时，保留旧登录能力，
不能把“恢复成功”写成“网页认证已完整”。扫码只能取得上游实际返回的字段，
不能保证 Panda 路径会提供 passport stoken。

## 尚未完成

1. 官方 passport QR → webVerifyForGame → comboWebLogin；凭据传递仍需按
   已保存的官方代码核对，不能把 Panda 的 Account token 无条件当作 stoken。
2. `35-4301` 的因果验证：补字段只是修复确定的数据丢失，不是证明服务端报错根因。
3. 1900 出站 ACK 关联、断线恢复/重传队列；本轮不宣称“可靠队列已完整”。
4. 用户协议/内置网页的真实展示及确认流程；旧桥的跳过逻辑仍需独立修复。
5. 音频增益最小值仍是 8 倍；本轮未改渲染/解码/音量设置，不承诺改善帧率或声音。
