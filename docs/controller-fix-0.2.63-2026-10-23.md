# 0.2.63 手柄修复交接（2026-10-23）

## 已确认的结果

在 yuzu 中复用已有加密登录存档，普通排队进入真实原神场景，原生手柄
右摇杆可以改变镜头，Start 可以打开派蒙菜单。不是把键鼠映射冒充原生手柄。
两次可见响应的记录分别为 `debug-0.2.62-idg-web-pad-trial3-20261023`
和 `debug-0.2.63-handheld-fixed-20261023`（工作区 outputs 下）。

最重要的遗漏在派发阶段：官方 APK `J5/a.java` 的 `k()` 给新版
`user_data` 加入 `idg`；`N()` 的旧版等价参数为 `-is_default_gamepad 1`。
当前 hk4e 请求发送 `idg=true`，仍由原有请求构造器整体签名。
不能在 StartGameReq 猜字段，不能把 63201 等本机 daemon IPC 发给 pod。

已跑通组合：终端 10、Xbox 360 VID/PID 045e/028e、鼠标点击标题页、
type-17 原生手柄游玩。服务端三个手柄位仍为 0，且没有回报 20037 mode=3，
但实际镜头和菜单响应成立。因此这两项只能作为诊断，不能据此禁用输入。
终端 3 的能力位为 1，但本轮没有视频，不作为交付默认配置。

## Joy-Con 掌机处理

- 机身两侧 L/R Joy-Con 从 `HidNpadIdType_Handheld` 读取，优先于外接手柄。
- 拆下后，系统已配成一对的 Joy-Con 从完整控制器玩家槽读取；Pro 手柄同理。
- 不把不同控制器的按钮或摇杆拼成一个手柄。
- 断开/切换时先交付中立输入；失败重试，然后才允许新来源按键通过。
- 12% 圆形死区，剩余行程重新缩放；没有额外平滑缓存。
- 游戏按键保持物理位置：Switch B/A/Y/X -> Xbox A/B/X/Y。
  游戏里的确认/取消仍由该账号的游戏设置决定；Minus 保留本地诊断。

参考实现是本机安装的 libnx `runtime/pad.h`；官方
[pad.c](https://github.com/switchbrew/libnx/blob/master/nx/source/runtime/pad.c)
说明 Handheld 和玩家槽的读取与合并语义。这里没有借由 yuzu 模拟蓝牙驱动。

## 测试与边界

真实 Switch 交叉构建通过；相关 host 测试通过。0.2.63 一轮记录：4553 解码帧，
0 解码错误，7991 音频包。音频包数量不等于听感验收。
最终发布前另跑 `debug-0.2.63-controller-release-20261023`，待交付 NRO SHA256：

`58717970157F3144714338F58D918BCD890F158EE81F98E8960558A73CBC7217`

最终这个哈希的两轮复测分别在 71 s / 21 s 收到 20005，随后信令断开。
这两轮未到手柄探测，不是本哈希的手柄验收通过，更不是长期稳定性通过。
此前 0.2.63 的镜头/菜单证据仍有效（最终改动仅移除了探测中的额外面板按键）。
监控现已把 SIGNALING_DISCONNECTED 计为失败，不能再显示无失败的“结束”。
10:10 又发现另一份不属于本测试的 yuzu GUI 进程；没有关闭或操纵它，
也不能只凭进程存在就认定两次断开的原因。后续同账号联调应避免并发会话。

模拟 SD 已隔离到原调试目录内的 `controller-test-isolated/sdmc`，不再与别的
调试修改共用终端配置。存档仅留在私有模拟 SD，不进仓库和发布包，不打印内容。

自测需 `cloudgenshin-autotest.txt` 加 `cloudgenshin-autotest.pad` 两个严格标记。
进入 SDK will_enter_game 后仅转镜头并打开菜单；不会继续猜测确认/取消键。
移除/改名标记即停用，发布包不含这些标记。默认标题页进入独立于自测标记，
且 SDK 开始进入游戏后停止，不在游玩中继续点击。

**尚未证明：真实 Switch 两侧 Joy-Con 手感、物理拆装、声音听感、长时间稳定性，
以及其他三款游戏同样可玩。yuzu 的脚本输入在 HID 采样之后注入，只验证后续
UI/编码/传输/云端链路。不要把这些 host/yuzu 证据写成整个项目完成。**

## 继续排查的位置

- `mihoyo/launch_flow.cpp`：默认手柄派发偏好。
- `mihoyo/controller_source_policy.hpp`：来源选择、死区、切换释放策略。
- `cloudgenshin_stream_view.cpp`：本地 HID 读取、输入交付和标题页生命周期。
- `cloudgenshin_stream_input.cpp`：终端匹配的指针点击。
- `scripts/cloudgenshin-monitor.ps1`：白名单状态监控；单个地址失败不能中断备用地址。

保持 TLS/证书验证；不切付费队列、不自动购买、不保存或输出二维码/账号令牌。
