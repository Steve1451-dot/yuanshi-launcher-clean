# 崩铁标题页输入修复与调试路径（2026-10-24）

## 本轮改动

崩铁（Combo app_id=8）在虚拟手柄可用时，标题页的手动 Continue、实体按键和
输入来源声明统一使用手柄。此前自动进入发送 XInput A，手动 Continue 却仍走
鼠标/触摸，实体 A/B 还会额外点击标题页；触摸终端也可能声明触摸来源后发送手柄。

新规则在 `mihoyo/launcher_input_policy.hpp`，用 app_id 和实际手柄传输状态选择
路线，不改物理按键的位置映射。没有虚拟手柄时保留原指针回退；原神、绝区零和
未知游戏保留原有行为。手动 Continue 沿用已有的 120 毫秒 XInput A 保持与松开逻辑。

这修复了本地输入路线的不一致，不代表线上 `1001_2` 已解决。没有更改设备指纹、
账号参数或网络验证策略。

## 离线检查

`mihoyo_launcher_input_policy` 覆盖崩铁标题页/游戏内、触摸/鼠标终端、无手柄回退、
原神/绝区零行为保留以及合成确认的按下/松开映射。
相关回归还包括 `mihoyo_game_profile`、`mihoyo_stream_input_policy`、
`mihoyo_launch_flow`、`mihoyo_dispatch_prepare`。

6 个相关宿主测试组通过，完整 Switch 构建通过（0.2.72）。本轮 yuzu 中已确认
`LOGIN_RESTORED`；普通队列位置 226 触发现有退出阈值，`QUEUE_LEFT` 成功，
未进入串流，不能据此验收标题页输入或 `1001_2`。用户随后要求暂停崩铁、转做终末地。
报告：`D:/desktop/2026-09-22/CloudGenshin-Debug-StarRail/reports/launcher-route-20261024-01`。

## 用户指定的两个 yuzu 路径

每次运行先检查实际存在的程序，不把其中一个写成唯一固定位置：

- 当前位置：`D:/files/yuzu-windows-optimized-pgo-ea-4176/yuzu-cmd.exe`。
- 旧环境位置：`H:/ALL/yuzu-windows-optimized-pgo-ea-4176/yuzu-cmd.exe`。

本轮已确认 D 盘程序存在，旧 H 盘位置当前未找到。用户表示后续两个位置都会使用。

崩铁使用独立运行目录：`D:/desktop/2026-09-22/CloudGenshin-Debug-StarRail`。
配置为 `yuzu-star-rail.ini`，模拟 SD 为 `runtime/sdmc`，报告放 `reports`。
登录材料仅用于本地运行，不属于源码或发布包。现有终末地目录与原始 yuzu 配置不改动。
自动验证只进行一轮普通队列尝试，超时或出现停止标记后先检查报告。
