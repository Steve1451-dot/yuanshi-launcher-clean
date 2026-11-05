# 原神连接稳定性修复（2026-10-24）

本轮修改 miHoYo RTC 会话、控制通道及对应测试。终末地相关实现由其他任务处理。
未更改产品版本或发布配置。

## 修复

- **缓冲区内的心跳被误判为超时。** WebSocket 的一次 poll 先解析旧缓冲区，
  再读取新数据；控制通道原来在新数据尚未解析时就检查超时。现在只在同步或
  心跳即将被判超时时，再执行一次有界 poll，让已收到的回复参与判断。
  新增测试已在修复前复现失败，修复后通过。没有延长无响应通道的超时。
- **累计媒体协商次数导致长会话主动退出。** 原限制是整个会话最多 32 次。
  现在只保留最近一分钟的接受时间，最多 32 项；正常长会话不再耗尽终身额度。
  短时协商风暴、无效 SDP、远端 ICE/指纹变化仍按原有边界处理。
- **控制通道瞬断立即结束串流。** 已就绪的 RTC 输入通道丢失后进入最多 5 秒
  的恢复等待，期间继续处理媒体并关闭输入发送；期限内恢复则重新开放输入，
  否则按原逻辑报告连接丢失。连续采样不能延长恢复期限，首次建立仍有独立超时。
  串流页面显示恢复状态；现有输入通道恢复逻辑重新声明设备和输入来源。

## 离线验证

7 个相关宿主测试组通过：`mihoyo_control_channel_policy`、
`mihoyo_web_control_channel`、`mihoyo_rtc_session`、`mihoyo_ui_language`、
`mihoyo_sdp_answer`、`mihoyo_stream_input_policy`、`websocket_client`。

回归包含：超时轮读到同步/心跳回复、真实静默超时、控制瞬断恢复与期限边界、
40 次分散媒体更新、同一分钟内第 33 次协商拒绝，以及恢复期间服务端结束会话
或信令断开的即时退出。所有传输均为合成测试，无账号或线上排队。

测试记录：`.opennow-msys-tmp/genshin-stability/host-regression.log`、
`.opennow-msys-tmp/genshin-stability/host-session.log`。

Switch 原生完整编译通过，生成 `build/switch/SwitchNOW.nro` 和配套 ELF，
构建脚本的版本检查通过（0.2.71）。NRO SHA256：
`C6331D52A1888D650F615AF7DFB6FF255540B4FA8337DEEB17AE28D166931736`。
记录：`.opennow-msys-tmp/genshin-stability/switch-build.log`。
这是当前共享工作目录的开发构建，包含目录中已有的其他未提交改动，不是独立发布包。

## 仍需实测

本轮没有真实原神串流或 Switch 真机验收。需要继续观察长时间游玩和短暂网络
抖动后的画面、声音、按键状态；服务端主动结束会话和不支持的 ICE 重启仍会退出。
这些离线修复不能证明所有断线来源已解决。
