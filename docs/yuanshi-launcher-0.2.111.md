# yuanshi launcher 0.2.111：独立目录与 Steve 仓库

日期：2026-11-04。按用户最新要求覆盖 0.2.110 的“保留旧目录”方案。

- 产品名 `yuanshi launcher`，入口 ys / zmd / bt，三合一与学习参考说明保留。
- 新的深色底、米白色双翼图标已在 resources/icon-source 与 resources/icon 中，
  不重新生成或替换用户选择的图。
- 构建产物为 `build/switch/yuanshi-launcher.nro`；SD 程序和数据目录为
  `sdmc:/switch/yuanshi-launcher/`。打包、快捷启动路径、日志、诊断脚本同步。
- 删除旧目录查找及自动迁移 API；不会读取、复制或重命名旧登录库。第一次使用
  需重新登录和设置本地偏好。不会自动删除旧目录，避免数据不可恢复。
- 只修改 libpeer 两处诊断文件路径为新目录，不改媒体协议、修复策略或队列参数。
- 新 NRO 沿用用户已有的全语言 NACP 填充步骤，应用作者为 Steve1451-dot；
  上游 LICENSE、版权和必要的来源说明继续保留。
- 原生包不再嵌入旧 HOME forwarder 安装器二进制；它的旧编译产物可能引用旧目录。
  原生页面没有该安装器入口；如开发继承的 GFN 工具，须按更新后的源码另行构建。
- GitHub 目标是用户的 `Steve1451-dot/yuanshi-launcher` 私有仓库；不向 upstream
  推送、不通过另一个已连接的 GitHub 账号写入、不擅自公开仓库或创建公开发布。

本轮为名称/安装边界调整，不代表 bt 的 1001_2 修复。安全检查不读取或输出用户
vault、密钥、Cookie、二维码或令牌；账号和运行时文件不加入提交。

另外核对了工作区已有的 SDK webview 全局 UA 补齐：官方 combo-web 的
getGlobalUserAgent 返回配置 ua 或空串，setGlobalUserAgent 保存 content 且不返回值。
现有补丁与该契约一致，保留 1024 字节上限和控制字符拒绝，不虚构 Android UA、
不改变 HTTP 身份或输出字段内容。对应合成桥测试通过，但未经新一轮真实入场验证。
