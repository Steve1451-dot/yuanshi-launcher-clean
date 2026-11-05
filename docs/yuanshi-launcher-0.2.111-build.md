# yuanshi launcher 0.2.111 构建校验

日期：2026-11-04。完整 Switch devkitA64 构建通过，最后增量打包通过。

- 程序：`yuanshi-launcher.nro`，30,318,289 字节。
- NRO SHA-256：`6646C8850B60EEDF9305F8C2495EF99654676E66445E012FEF7C408397FAA09A`。
- 对应 ELF SHA-256：`62988DB7587DA4425B6A39D3EB953DE7B585601A725CCBBA94BDA82FFB0EB40A`。
- 校验 NRO0 / ASET / NACP 边界、16 个语言槽标题、版本 0.2.111、作者 Steve1451-dot。
- 嵌入图标逐字节匹配用户选定的 `resources/icon/icon.jpg`。
- 原生 RomFS 不包含旧 HOME forwarder 安装器；未更改用户选定的图标。
- 独立目录、RTC 会话、语言、网络延迟取值和透明绘制测试通过；安全日志监控、
  模拟器配置守卫测试通过。SDK webview 的合成测试通过，非真实入场证明。
- 活跃源码不再引用旧 SD 目录或 LegacyAppHomePath；第一次使用新目录需重新登录。

构建仍有已有第三方 stb 初始化警告，没有将其冒充为新代码错误或隐藏成“无警告”。
没有实机回归、没有再次消耗云时长，没有证明 bt 的 1001_2 已解决。
包中不含账号、密钥、vault、APK、自动排队标记或设备信息同意标记。
