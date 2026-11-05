# 本地状态与退出流程续修（2026-10-26，Asia/Shanghai）

继续上一轮 Endfield / Star Rail 手柄代码。用户要求检查并直接完善当前环境能确定的部分；仍没有反编译、yuzu 或实机验证条件。本轮没有新增或执行宿主测试，没有启动游戏或操作账号；检查范围为源码、脚本语法和完整 Switch 交叉构建。

## 已修复

1. **画质与帧率保存混入未发送成功的选择。** 原 `PersistStreamSettings()` 在任一发送成功后读取两个 UI 请求快照。例如画质成功、帧率仍在排队或发送失败时，也会把新的帧率写入 SD。现在网络线程分别记录实际被本地发送接受的两个值，只保存这些值及此前有效的保存值。首次没有保存数据时，等待两项都发送成功后再写入；快速更改 UI 不会把尚未发送的新选择混进去。启动时初始化已保存的值，避免同样设置每次启动都重写。这里的成功仍指本地发送，未宣称服务器已经应用。
2. **触摸编号 0 漏释放。** `TouchTracker` 以前用数字 0 同时表示“没有主触摸”和合法的 finger id。主触摸为 0 时，`release_all()` 会跳过它，多个触点的主触摸标记也可能出错。改为可选编号，正常跟踪、标记和释放编号 0。Switch 输入驱动直接传递 HID 的 `finger_id`，本地路径没有排除 0。
3. **返回页面时访问已经销毁的视图。** 仓库内 Borealis `View::hide()` 对已经隐藏的视图立即调用完成回调；`Application::popActivity()` 的完成回调会删除旧 Activity。Endfield 先前在 pop 后仍读取 `progress_`、`automated_seconds_` 等成员。现在 pop 前复制后续需要的回调和数据；pop 后只使用副本。Genshin / Star Rail 的 pop 也移到 UI 任务中，避免从 `draw()` 内同步删除正在绘制的视图。
4. **延迟任务的页面与生命周期保护。** 两个串流视图的返回任务均持有弱生命周期标记，页面销毁后自动失效；执行前检查栈顶确实是原串流页面。手动重连等待旧 Activity 完成销毁，再执行现有的新启动流程。启动页回调同样检查生命周期、游戏选择、登录和是否已有任务，避免回调落到已销毁或已经切换的页面。返回事件补充中文说明。
5. **手动重连被旧取消标志阻止。** 启动页 `willDisappear()` 会把旧任务的 `cancelled` 置为 true；原重连回调直接据此拒绝新操作。现在用户点选的新请求检查当前有效状态，交给既有 `begin()` 为新任务重置取消标志。仍由用户主动点选，不新增自动重连、会话续接或新协议报文。
6. **PowerShell 构建入口明确报告失败。** `build-switch.ps1` 现在检查 Bash 的退出码，非零即抛出构建失败，便于上层脚本正确停止。

## 检查结果

- 变更文件 `git diff --check` 通过。
- PowerShell Parser 对 `build-switch.ps1` 的语法检查通过；`bash -n scripts/build-switch-msys2.sh` 通过。
- 使用 `OPENNOW_BUILD_DIR=build/switch-controller`、`OPENNOW_BUILD_JOBS=8`、`CLOUDGENSHIN_DEBUG_SYMBOLS=ON` 调用 `./build-switch.ps1`，完整 devkitA64/libnx 编译、链接、NRO 生成及版本检查通过。手动重连收尾修改后已重新构建。
- 日志：`build/local-fixes-build.log`、`build/local-fixes-final-build.log`。

## 产物

沿用源码中的 **0.2.74** 版本文本，未发布新版本。上一轮 `SwitchNOW-controller-20261026` 文件保留；本轮单独保存：

- `build/switch/SwitchNOW-local-fixes-20261026.nro`，40,905,564 字节。
- NRO SHA-256：`864AC403D27FC6ED269335D2877CAF32D367239B4D53B16F8D7CC29D3F2C2276`。
- `build/switch/SwitchNOW-local-fixes-20261026.elf`。
- ELF SHA-256：`F388FF6BC2EA34B0073A1788CB0BC4156DC63A492D922F06F93A0CBE26496418`。
- 两个副本均已与原始构建产物核对 SHA-256。

编译成功仅验证代码与构建链路。画质保存失败分支、触摸释放、动画/退出时序、手动重连和实际游戏响应仍待可运行环境验证。自动重连、云会话续接、码率自适应、丢包与解码缓冲调参继续留待可测试环境，不据本轮结果认定 Endfield 卡片头或 Star Rail 登录问题已解决。
