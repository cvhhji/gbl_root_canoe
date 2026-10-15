# 假回锁 6.2.221（测试分支）

- 同步上游 `8cc6fa5`，增加 Advanced 菜单、分组卷浏览器和 USB Mass Storage 导出。
- 增加 `efisp.fat` 容器读取与启动参数，继续支持原有 `persist/efisp` 布局。
- 同步 Fastboot、GPT、USB 序列号及 ext4 修复，保留本仓库双缓冲边界与标准 GPT 兼容性。
- 启动菜单、自带工具和 SuperFastboot 增加中文，修正菜单比例和居中位置。
- 首次默认英文，语言入口显示 `Language / 语言`。选择后自动保存，更新保留；清除 `efisp` 目录重新安装后恢复英文。
- 关闭交互菜单和 SuperFastboot 的 UEFI 看门狗，避免久置后重启。
- WebUI 只保留一个 `index.html`，Android `extractfv` 缩小到约 319 KiB。
- 产物不带 LICENSE、NOTICE、代码注释和无用文本，相关许可文本保留在源码仓库。

本版本用于 `test/upstream-20261011` 分支的云端构建和设备测试。双语菜单及打包调整同步主分支，其余上游更新仍在测试分支。尚需真机验证本次布局、语言保存及长时间停留后的表现。
