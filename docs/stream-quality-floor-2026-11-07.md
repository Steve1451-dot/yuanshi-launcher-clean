# ys / zmd 串流码率与悬浮窗设置

本次修复使用现有协议和有界解码队列，不改账户、输入包或密钥存储。

| 画质档 | 最低请求码率 | 最高请求码率 |
| --- | --- | --- |
| 高清 | 6000 kbps | 12000 kbps |
| 省流 | 4000 kbps | 8000 kbps |
| 极省流 | 3000 kbps | 4000 kbps |

终末地的自适应上限仍只由传输损伤触发，不能降到所选档位下限以下。
实际视频 payload 速率受静态场景和服务端编码器影响；请求范围不是实测保证。

原神现在在 `RtcSetGraphicsMode` 后发送独立的 `RtcSetBitrate`（20019），
字段 1/2 是 uint32 最低/最高 bit/s。运行时范围与派发码率均按照官方网页
将界面 kbps 乘 1024；终末地的 WeLink SimpleCmd 仍使用十进制 kbps。
原神派发会读取对应游戏已保存的画质/帧率，避免高画质一直使用低档派发上限。

协议核对来源（2026-11-07，从 [官方云原神网页](https://ys.mihoyo.com/cloud/) 引用）：

- [官方网页 SDK](https://webstatic.mihoyo.com/common/clgm-web-app/ys/js/cg-sdk.891daeae.js)：
  `RtcSetBitrate.serializeBinaryToWriter`、`setBitrate`、`setGraphicsMode`、`setFps`。
- [官方网页界面](https://webstatic.mihoyo.com/common/clgm-web-app/ys/js/web.103b867a.js)：
  `setSpeedLimitGear` 与 `getPaasDispatchPayload` 的单位转换。

两条串流链路都在首个解码画面及控制通道就绪后应用设置，每个 UI revision
最多尝试 20 次，间隔至少 500 ms，需要三次成功本地发送后才标记 Request sent。
这用于覆盖 pod 启动时可能忽略首个设置的窗口，不代表收到服务端 ACK。
恢复连接时重发当前选择；旧 revision 的完成结果不能覆盖用户的新选择。
切换画质后重新应用 FPS，成功发送 FPS 时同步本地解码队列的帧率参数，
最终请求一次受现有节流保护的关键帧。稳定游戏期间不持续发送设置。

原神长队退出提示弹窗已移除；状态卡、取消/清理以及现有超过 100 人退出策略保留。

回归测试包括协议金字节、生产 Session 的代理包、保存选择的派发请求、
失败/重连/新 revision 的预算，以及传输损伤时各档下限不会被穿透。
完整登录、实际编码码率与 30/60 FPS 切换效果需要 Switch 和真实账户验证。

验证结果：13 项相关主机测试通过；`./build-switch.ps1` 完整构建及版本校验通过，
产物为 `build/switch/yuanshi-launcher.nro`，版本标签保持 0.2.111。
