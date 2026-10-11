# 假回锁 6.2.220（测试分支）

- 同步上游 `8cc6fa5`，增加 Advanced 菜单、分组卷浏览器和 USB Mass Storage 导出。
- 增加 `efisp.fat` 容器读取与启动参数，继续支持原有 `persist/efisp` 布局。
- 同步 Fastboot、GPT、USB 序列号及 ext4 修复，保留本仓库双缓冲边界与标准 GPT 兼容性。
- 包含主分支 6.2.219 的 APatch、FolkPatch WebUI 兼容修复，界面不增加常驻提示。

本版本只用于 `test/upstream-20261011` 分支的云端构建和后续设备验证，未合入主分支。源码审查及构建结果不能替代真机启动、USB 导出或分区写入验证。
