# Endfield / Star Rail 手柄续修（2026-10-26，Asia/Shanghai）

本轮在 H 盘工作区继续既有 0.2.74 进度。用户确认当前无法进行反编译和 yuzu 测试，要求先完成能确定的代码，依赖实际测试判断的改动留到后续。没有新增或执行宿主测试，没有运行游戏、排队、扫码或操作账号；验证为源码检查、脚本语法检查与完整 Switch 交叉构建。原有工作区改动保留。

## 手柄代码

- `app/src/mihoyo/controller_sampler.hpp`：Endfield 与 Genshin/Star Rail 共用本地采样。掌机 Joy-Con 优先；拆下后从完整玩家手柄中选择，保留当前有效来源，不合并不同玩家的摇杆/按钮。覆盖四方向、ABXY、肩键、数字扳机、双摇杆按下、Plus/Minus 和四轴；沿用 12% 圆形死区及剩余行程缩放。Switch 直接读取独立 libnx PadState，省去本视图此前重复读取的 Borealis 统一状态。
- Endfield 的 Switch 路径原先遗漏 `plus/minus` 赋值，`+` 因此不能成为远端 Start。现在完整采样后，Minus 留给本地诊断，Plus 正常映射 Start。物理 Switch B/A/Y/X 仍对应 Xbox A/B/X/Y。
- `app/src/mihoyo/controller_capture_policy.hpp`：本地捕获与完整输入快照。Endfield 将连接状态、按键与捕获代数放在同一个受锁保护的快照中；来源切换、面板操作、断开与重新连接都要求先交付中立状态，再接收后续按键。界面恢复后等待物理控件松开，避免面板确认键进入游戏。
- `app/src/endfield/xinput_state.hpp`：在原有 CtrlMsg V1 增量 / CtrlMsgV2 完整快照编码之上加入中立交接。仅缓存本地接受的发送；部分 V1 字段失败时保留成功字段，重试失败字段，期间阻止新来源输入。断开先中立再分离虚拟手柄；重新连接先发中立快照。小幅摇杆跨零反向不再被 512 增量阈值吞掉。
- 两条串流路径共同使用退出组合键捕获：`+/-` 同时按下后，直到两者都松开才恢复远端 Plus。切换手柄时清除旧来源的退出长按计时。
- Star Rail 本来就由 `CloudGenshinStreamView` 承载。本轮已在这个共同入口接入共享采样和组合键处理；网络线程在同一次加锁中读取输入与代数。面板/浮球操作立即更新代数，即使打开和关闭发生于两次网络轮询之间，也保留中立交接要求。Star Rail 标题页现有 app_id=8 手柄路线继续保留；各游戏使用各自原有的线上编码。

## 本地保护与便利改进

- Endfield 仅在原有启动握手与输入通道都就绪后处理普通手柄输入，避免启动时反复构造无效报文。
- 游戏桥描述的虚拟连接状态跟随本地接受的连接报文，而非异步读取物理连接标志。它表示本地发送状态，不能视为云端或游戏确认。
- 退出时为中立与断开提供 100 ms 的本地重试预算，然后进入原有会话退出排空。单次网络轮询与调度可能略超出预算；通道已丢失时无法承诺远端收到释放。
- 5 秒诊断快照增加手柄报文本地接受/失败计数、输入就绪、待交付中立、实际输入协议版本，并补齐相关中文说明。没有逐帧/逐包日志，也不输出原始按键序列或凭据。
- Endfield 诊断补充位置映射说明，日志路径改为由 `AppHomePath()` 生成。
- `scripts/build-switch-msys2.sh` 新增可选 `OPENNOW_BUILD_DIR`，默认仍为 `build/switch`。当前旧目录的 CMake 缓存来自 C 盘，所以本轮在独立 `build/switch-controller` 重新配置和完整构建，避免覆盖旧构建树。

## 验证与产物

完整构建命令（PowerShell）：

```powershell
$env:OPENNOW_BUILD_DIR='build/switch-controller'
$env:OPENNOW_BUILD_JOBS='8'
$env:CLOUDGENSHIN_DEBUG_SYMBOLS='ON'
./build-switch.ps1
```

完整 devkitA64/libnx 构建、链接与 NRO 生成通过，版本字符串检查通过。收尾改动已重新构建。变更文件的 `git diff --check` 与构建脚本 `bash -n` 检查通过。没有把编译通过当作手柄、线上串流或游戏可玩的验收。

产物仍使用源码中的 **0.2.74** 版本文本；这是本地续修产物，未进行版本发布。为避免与旧同版本包混淆，新产物使用独立文件名，并保留匹配 ELF：

- `build/switch/SwitchNOW-controller-20261026.nro`，40,901,468 字节。
- NRO SHA-256：`FC2A3877C3FBDD4FA7EEB67CDA7111B901670A93E0F6AA5D8A8E35017C46D737`。
- `build/switch/SwitchNOW-controller-20261026.elf`。
- ELF SHA-256：`68E4658A9176F4161DCC044A8B6BB515393A6C08F94C537EA9246A9CC031223A`。
- 两个文件均已与 `build/switch-controller` 的原始构建产物核对摘要。
- 构建日志：`build/controller-build.log`、`build/controller-final-build.log`。

## 留待可测试环境

没有新增自动重连/云会话续接、码率自适应或新的丢包/解码缓冲调参，也没有猜测新的云端字段。后续需要在可测试环境验证 Endfield/Star Rail 实际手柄响应、Joy-Con 拆装、面板开关、组合键释放、失败重试，以及断网后远端的真实状态。Endfield 既有卡片头/游戏初始化问题与 Star Rail 既有线上登录问题，不能由本轮编译结果认定已解决。

## 后续：终末地面键布局（2026-10-26 晚）

终末地现在读取共享 Face buttons 设置，默认 Xbox 物理位置，Switch 为标签映射。宿主测试和完整 NRO 构建通过；详见 `docs/endfield-controller-layout-2026-10-26.md`。
