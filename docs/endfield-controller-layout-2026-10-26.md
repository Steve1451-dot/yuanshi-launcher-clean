# 终末地手柄面键布局续修（2026-10-26）

本轮只处理可以离线确定的手柄映射：让终末地读取与原神/星穹铁道相同的
“Face buttons”设置，并保持既有 Xbox 360 虚拟手柄、无振动和物理来源
切换逻辑不变。没有启动队列、登录、扫码或游戏测试，也没有读取任何
凭据、vault 或 key。

## 改动

- `app/src/controller_layout.hpp`
  - `MapFaceButtons` 的参数从 `const std::string&` 改为
    `std::string_view`，避免每帧构造字符串；Xbox/Switch 的映射规则不变。
- `app/src/mihoyo/input_packet.*`
  - `ToGamepadState` 新增可选的 `controller_layout` 参数，默认 `"Xbox"`。
  - 面键统一复用 `opennow::MapFaceButtons`，不再在输入包实现里重复一套
    A/B/X/Y 判断。
  - 默认布局仍保持物理位置：Switch B(下)→Xbox A、A(右)→B、Y(左)→X、
    X(上)→Y；`"Switch"` 布局改为标签一致：A→A、B→B、X→X、Y→Y。
- `app/src/endfield/xinput_state.hpp`
  - `SendCapturedXInput` 增加 `controller_layout` 参数，并在映射
    `ControllerFrame` 时传给 `ToGamepadState`。
  - V1 增量、V2 完整快照、连接/断开和中立交接路径共用同一映射结果。
- `app/src/endfield_stream_view.*`
  - 构造时读取一次 `opennow::LoadStreamSettings().controller_layout`。
  - 网络线程每帧把当前布局传给 `SendCapturedXInput`；设置只在构造时读取，
    不会在串流中并发修改。
  - 诊断帮助文字按当前布局显示实际面键说明。
  - 新增一次性安全事件 `ENDFIELD_CONTROLLER_LAYOUT`：0 表示 Xbox 物理位置，
    1 表示 Switch 标签；不包含账号或输入内容。
- `tests/mihoyo_input_packet_test.cpp`
  - 覆盖 Xbox 默认布局、Switch 标签布局，以及未知设置值回退到物理位置。
- `tests/endfield_xinput_state_test.cpp`
  - 覆盖终末地 V2 完整快照在两种布局下的面键位；同一物理 Switch A 在
    Xbox 布局下发出 XInput B，在 Switch 布局下发出 XInput A。
  - 覆盖 V1 增量路径同样遵循布局设置。

## 复核过的既有行为

- 虚拟手柄仍报告 Xbox 360：VID/PID `0x045e/0x028e`，GameBridge 仍声明
  Xbox 类型 11、完整控制类型 1。
- 方向键、Plus、L/R、ZL/ZR、双摇杆、摇杆按下均沿用共享采样；Minus 保留
  本地诊断，不发送到远端。
- 摇杆继续使用 12% 圆形死区并重缩放剩余行程。
- 连接、断开、来源切换和面板开关都先交付中立状态，再允许新输入。
- 云输入路径没有加入振动/rumble 代码。

## 验证

宿主测试全部通过（MinGW g++ 16.1.0，`-std=c++20 -Wall -Wextra -Werror`）：

- `controller_layout_test`
- `mihoyo_input_packet_test`
- `endfield_xinput_state_test`
- `mihoyo_controller_source_policy_test`
- `endfield_wl_input_test`
- `mihoyo_stream_input_policy_test`
- `controller_delivery_policy_test`
- `controller_assignment_policy_test`

完整 devkitA64/libnx 构建通过：

```text
OPENNOW_BUILD_DIR=build/switch
OPENNOW_BUILD_JOBS=8
./build-switch.ps1
```

构建日志：`build/mapping-build.log`。构建前已把同一目录里上一版
`SwitchNOW.nro` / `SwitchNOW.elf` 备份为
`SwitchNOW-prior-20261026-1736.*`，没有覆盖历史产物。

## 产物

- `outputs/controller-mapping-20261026/SwitchNOW-endfield-layout-0.2.74.nro`
  - 40,917,852 字节
  - SHA-256 `C79D7286EE09F46C70E6AE51BDC8A2A542D3746F501E78E843EAD2DE63948E7F`
- `outputs/controller-mapping-20261026/SwitchNOW-endfield-layout-0.2.74.elf`
  - SHA-256 `4B65B81600D7319D95F69CBB2B612E1E44DBE47C0FDBD47ED1882067E4914D46`

该 NRO 基于当前 0.2.74 工作区，包含 `9287EAFB...`（sdk-retry-v2）已有的
GameBridge 重试恢复以及本轮面键布局改动；不是新的版本发布，也不替代
`outputs/controller-check-20261026/` 下的冻结对照包。不同 NRO/ELF 的测试
结果不能混用。

## 仍需实机确认

- 两种布局在游戏内的确认/取消、菜单和战斗按键是否与提示一致。
- Joy-Con 掌机、拆下后的配对 Joy-Con、Pro 手柄的来源切换。
- 断线、重连、面板开关后的中立交接和远端真实状态。
- 以上均未在真实 Switch 或可玩会话中验证；编译与宿主测试不能替代实机结论。
