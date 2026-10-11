---
name: "BL Flasher WebUI"
description: "假回锁模块的槽位、刷写任务与日志界面"
colors:
  accent: "#ff7a00"
  background: "#f7efe2"
  ink: "#1b140f"
omitted:
  - section: typography
    reason: "沿用 webroot/style.css 中现有字体栈"
  - section: spacing
    reason: "沿用 webroot/style.css 中现有布局"
  - section: rounded
    reason: "沿用 webroot/style.css 中现有圆角变量"
components:
  button: {}
  dialog: {}
  status: {}
---

# WebUI 维护约定

面向在 Android Root 管理器中操作模块的用户。支持中文和英文；槽位、后台任务及失败原因优先显示在现有状态区域。

本轮只修复 KernelSU、APatch、FolkPatch 的宿主接口兼容，保留原有文字、配色和布局，不增加常驻兼容性提示。视觉取值的唯一实现来源是 `targets/magisk_module/module/webroot/style.css`，此文件记录维护约定，不生成或覆盖 CSS。

宿主差异由 `webroot/kernelsu.js` 统一处理。KernelSU 使用原生模块信息；没有 `moduleInfo()` 的宿主使用模块固定 ID 对应的目录。语言翻译由 `app.js` 的 `i18n` 管理；KernelSU 使用持久模块配置，APatch、FolkPatch 沿用已本地化的模块名称与描述。

分区按钮在初始化期间禁用。只有命令接口可用、模块目录匹配并读到有效的相反槽位后才启用；任务运行时保持禁用。失败通过现有状态标签和任务消息报告，保留已有二次确认流程。

验证入口：`node --test tests/webui_compat.test.mjs`、`python3 -m unittest discover -s tests -p 'test_*.py'`。测试使用模拟宿主和临时模块目录，不构成手机刷写验证。
