# 0.2.76 终末地触摸悬浮设置

用户要求与原神风格、触摸操作一致，明确不加 L+R；默认最低码率、30 帧。

## 实现

- `endfield_stream_overlay.cpp` 独立拥有终末地浮层输入和绘制，不继续膨胀串流
  主文件。复用原神的 FloatingBall、MenuTap、Layout、SettingRequest、StreamRates；
  颜色、尺寸、滑杆图标、11 行菜单和操作反馈一致，触摸打开/拖动贴边。
- 没有给终末地注册 L+R 快捷键。关闭浮层时该组合仍是游戏输入；打开后通过
  原有 ControllerCapture generation 中立交接隔离输入，防止按住 A 等按键泄漏。
  未改原神已有操作或 Xbox/Switch 面键映射。切换控制器来源时重置菜单边沿。
- 面板提供三档码率、30/60 FPS、实测 HUD、语言、诊断、日志、标题继续、退出。
  停止需二次确认，同一帧触摸提交不再执行实体 A；标题继续仍受生命周期保护，
  仅显式用户操作才发送一次已有的手柄 A 按下/松开。

## 设置与协议

`endfield/stream_settings.hpp` 独立保存设置和策略，路径由 AppHomePath 派生，
文件为 `endfield-stream-settings.txt`。不能复用 miHoYo 的 graphics-mode 文件：
其值与终末地的码率档不是同一协议。未保存/无效/超长文件回退 tier=2、FPS=30；
允许恢复原子替换中断留下的有效 .bak，成功替换不保证永久保留 .bak。

默认低档范围 1000–4000 kbps，中档 3000–8000，高档 4000–12000，来自本地
官方网页 bundle 的 BIT_RATE_LEVEL_MAP。`setBitrateRange(min,max)` 和
`changBitrate/autoBitrateAdjust` 已核对：上限用 ResetVideoBitrate、下限用
autoBitrateAdjust，十进制 kbps 写 SimpleCmd.arg。若新 max<=旧 min，先降下限，
否则先设上限。两次写都被接受才记住新范围；部分失败仍可有界重试，不算成功。

DispatchInputs 默认 FPS=30、bit_rate=4000；实际使用独立已保存选择构建派发。
原本固定派发 60、首帧后才请求 30 的路径已统一。每次分配将设置快照随
AllocatedStream 传入，避免启动时重新读文件与派发值不一致。

首帧后才允许 SetFps/码率请求；SDK intercept/PipelineConnected 时序不变。
每个 UI revision 独立 500 ms/最多 20 次写预算，帧率与码率不互相消耗，也不让
初始尝试耗尽后永久禁用菜单。复用原子 SettingRequest，旧发送结果不会覆盖
新的 UI 选择。只有接受完整请求的值可持久化，不保存尚未发送的另一项。

HUD 明确为解码 FPS / 完成的视频负载 Mbps，不作带宽/RTT；请求发送、服务器
实际生效分开表述。可选设置失败只作诊断，不冒充整个串流断开。

## 验证与产物

7 项 host：endfield_stream_settings、endfield_cloud_http、mihoyo_stream_overlay_policy、
endfield_xinput_state、endfield_launcher_status、endfield_post_frame_policy、mihoyo_ui_language。
安全监测测试通过；之前同轮 6 项 DTLS/UDP ctest 通过。
补齐 xinput host 目标缺失的公共 input_packet.cpp 链接依赖，此为测试脚本缺口，
不是又改一套手柄映射。新设置测试最初误以为成功原子替换会留下 .bak，已按既有
helper 行为改为显式模拟中断备份，不修改公共文件替换实现。

完整 NRO 构建、首帧保护增量及中断后的最终复核均通过，版本/新增常量已核对。
CloudGenshin/NACP 标签由现有单一版本源得到 0.2.76，OpenNOW/forwarder 自身
版本未混改。测试包只含 README 和 NRO：

`outputs/CloudGenshin-NX-0.2.76-endfield-touch-overlay-alpha.zip`

NRO 40954716 字节，SHA-256：
`BD2C9537C948B4B9EBDEDF0EF00854419FDBC10F0EEFF780B91D0C21032FF0C2`

本版没有线上排队或真实 Switch 验证；不把主机手势测试当成视觉验收或服务端
设置 ACK。用户的实机启动失败仍须新鲜错误码；0.2.75 的错误提示修复仍保留。
没有读取、输出或打包任何账号材料，没有新定时任务或后台自动排队。
