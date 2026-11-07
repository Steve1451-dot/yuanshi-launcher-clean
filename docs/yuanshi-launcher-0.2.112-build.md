# yuanshi launcher 0.2.112 构建校验

日期：2026-11-07。完整 devkitA64 Switch 构建通过，回执空回调修复后重新链接并打包通过。

- NRO SHA-256：`039056E6F31472627FA3234645B31101B2D7DA90DAC935E503DE8F7E43E03986`。
- ELF SHA-256：`9B2B30F2B88677763064FD9BD21C894B01BC90D8105DF20FFC44710938A0338D`。
- HOME 快捷入口 NRO SHA-256：`713840C233AAFAF58AFEDD846B448E0D565AAD72C8ABDE99930F5FF0ED843055`。
- NRO0/ASET/NACP 校验通过：版本 0.2.112、16 个语言槽名称、Steve1451-dot 作者、
  嵌入图标与 resources/icon/icon.jpg 一致。快捷入口沿用同一 CMake 版本。
- 原生 RomFS 不含旧 OpenNOW forwarder 安装器，不打包 APK、反编译代码或个人运行数据。

15 项主机回归通过：genshin_gamepad_mode、controller_mapping、controller_mapping_editor、
controller_mapping_draw、controller_mapping_diagram、compact_stream_menu、compact_stream_menu_draw、
mihoyo_ui_language、controller_system_buttons、genshin_input_experiment、gyro_stick_assist、
mihoyo_input_packet、mihoyo_rtc_session、mihoyo_launch_flow、mihoyo_launcher_input_policy。
覆盖原生输入绕过本地表、所有按钮与轴的编码、模式保存与备份恢复、保存失败回退、
本地菜单隔离及旧配置保留。RTC 额外回归确认：本地发送不是云端回执，真实模式更新、
坏包不覆盖状态，未启用诊断也能处理。该测试先暴露空回调异常，修复后重跑通过。

中英文页面绘制模型测试通过，并检查了合成预览中的原生按钮页和操作模式页。
预览不是模拟器或实机截图。构建有原有第三方 stb 初始化警告。

未进行新的账号登录和 Switch 实机测试。游戏内能否修改目标动作及跨云端实例保留
尚未验证；构建和离线协议测试不能代替该验证。
