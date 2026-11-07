# 云原神 7.2.0 原生手柄与游戏内键位

日期：2026-11-07。本次重新下载官方 Android APK 并实际反编译，没有使用账号、
捕获个人会话或向云端发送猜测的配置命令。厂商 APK、反编译源码和原生库仅留在
忽略的本地构建目录，没有加入仓库或产品。

## 样本与复查

- 入口：[云原神官网](https://ys.mihoyo.com/cloud/)。页面配置为 7.2.0。
- 下载入口：`https://api-takumi.mihoyo.com/event/download_porter/link/clgm_cn/official/android_web`。
- 官网内容接口：`content_v2_user/app/f89089f8b4f4476c/getContent`，iAppId=74、
  iInfoId=111945、sLangKey=zh-cn，取 Android_DEFAULT 项。
- 包名 `com.miHoYo.cloudgames.ys`，AndroidManifest 版本 7.2.0，versionCode=400000048。
- APK：85,326,353 字节；SHA-256
  `F16ED4ADD7D2B1105B2980580FAD524CC9CE115E9863EB98790DD260EB50E73C`。
- 使用官方 [JADX 1.5.6](https://github.com/skylot/jadx/releases/tag/v1.5.6)，处理 9,988 个类，
  报告 118 处反编译错误；相关手柄方法可读，不能据此宣称所有方法均已完整还原。
- 本地样本：`build/reference/android/yscloud-official-20261107.apk`；Java 输出：
  `build/reference/android/yscloud-20261107/jadx/sources/`。

## 已确认的协议路径

1. `com/p043mihoyo/cgsdk/GamepadManager.java:275` 的 sendGamepadState 读取
   XinputData，上传设备 VID/PID、按钮、扳机、左右摇杆，不带攻击、跳跃等动作名。
   `ClientCore.java:715` 的 startGamepad 只启动设备管理；syncGamepadList 同步设备列表。
2. `M5/a.java:1156` 将 `c(true)` 写入签名启动数据的 `user_data.idg`。
   该函数根据设备配置读取 is_default_gamepad（false 分支读 gamepad_enabled）。
   旧命令行拼写是 `-is_default_gamepad 1`。本项目已有 hk4e/hkrpg 的 idg=true
   和 using_new_cmd_line=true，无需猜造新命令或改登录终端身份。
3. `MihoyoSdkHolder.java` 的 onGamepadDeviceChanged 处理 SDK 界面与设备能力，
   不是游戏动作绑定保存。reset input 也是释放输入，不是提交键位表。
4. arm64 `libnetio.so` 的 common/datachannel.proto descriptor 仍有
   RtcGamepadList（字段 1/2：VID/PID 数组）与 RtcGamepadKey（字段 1–10：
   VID/PID、buttons、双扳机、四摇杆轴、RTT）。与本项目已有编码一致。
5. `libclient_core_impl.so` 的 onGamepadDevices（0x869e0）创建设备列表，
   onGamepadPressed（0x865f0）提交状态。反汇编可见 RtcDataChannel、RtcGamepadList
   与 netio::Worker 路径，不是动作绑定表上传。
6. common/model.proto 中 SdkGameConfig 是串流能力；CaGameConfig 的 Xbox/PS 等是
   支持标志。服务端 SyncGameControlMode.mode=3 是进入手柄模式的回执，
   不代表游戏内键位已经保存。

可使用仓库的只读解析器复查，不依赖 JADX 的 Java 重建质量：

```text
python scripts/inspect-apk-descriptor-fields.py build/reference/android/yscloud-20261107/unpacked/lib/arm64-v8a/libnetio.so common/datachannel.proto --message Gamepad
python scripts/inspect-apk-descriptor-fields.py build/reference/android/yscloud-20261107/unpacked/lib/arm64-v8a/libnetio.so common/model.proto --message GameConfig
```

## 0.2.112 的实现

原神默认启用游戏原生手柄直通。Switch HID 只经过已有标准 Xbox 编码，
不套本地战斗映射、L 轮盘特例或体感转右摇杆。物理按钮到 Xbox 按钮位置的
协议编码仍然需要保留，它不决定游戏动作。虚拟设备身份、实时输入和重连恢复
继续使用现有 Session；启动提示 idg 也保持原协议。

`genshin-gamepad-mode.txt` 位于 AppHomePath()，只保存 Native/LocalMapping。
选择先原子写 SD，成功后释放旧输入并切换；保存失败继续旧模式。加载支持备份，
没有有效记录时选 Native。已有本地映射与终端配置不会被该模式覆盖。
原生界面只显示标准按钮，提示到游戏内改键；需要旧行为时可切回本地映射。
这项新默认仅适用于原神，其他游戏原有输入配置继续按各自设置工作。

## 尚未证实的部分与实机验收

未找到有明确调用方、参数、成功回包及跨会话效果的“直接导入游戏动作绑定表”
接口。静态结果不证明私有、动态或未成功反编译的代码中一定没有此接口。
本版保存的是客户端输入模式，不能称作已经永久写入云端游戏键位。

安装候选 NRO 后，进入原神的游戏设置，确认游戏识别手柄并允许修改目标动作。
先改一个容易辨认的动作并实际触发，关闭本地悬浮菜单再验证按键行为；随后退出
串流、重新进入，再检查同一动作。只有重开后仍保留，才能认定该账号和云端环境
支持跨会话保留。记录游戏内键位页和原生模式回执，有助于区别客户端直通、
游戏限制与云端配置持久化；本次尚未进行该硬件测试。
