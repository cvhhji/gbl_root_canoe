# 2026-10-11 上游同步记录

测试分支：`test/upstream-20261011`，版本 `6.2.220`。主分支 `6.2.219` 仅包含 WebUI 兼容修复。本次同步没有合入主分支。

来源：[SuperTurtleDev/gbl_root_canoe](https://github.com/SuperTurtleDev/gbl_root_canoe)，截至提交 [`8cc6fa5ad467af6d34a00f401290035ee87a14c7`](https://github.com/SuperTurtleDev/gbl_root_canoe/commit/8cc6fa5ad467af6d34a00f401290035ee87a14c7)。与本仓库共有祖先为 `0ec981a0ef12848ac262dbd50234ee3288245938`，上游一侧共有 24 条新增历史记录，包含合并和撤销提交。

## 移植内容

| 范围 | 行为 |
| --- | --- |
| Advanced 与卷浏览器 | 按 UFS LUN 和虚拟磁盘分组，保留旧启动项及子菜单行为，修复返回键消抖 |
| USB Mass Storage | 导出 LUN、分区及已挂载的 `efisp.fat` 虚拟磁盘，操作时选择读写模式 |
| 启动存储 | 为 `efisp.fat` 建立固定 extent 映射并作为 FAT 卷读取，继续扫描原有 `persist/efisp` |
| 启动项 | 解析可选参数并通过 EFI `LoadOptions` 传递，兼容旧记录格式 |
| Fastboot | 限制响应长度，使用设备序列号，退出后返回菜单，GPT 更新错误向调用方传播并恢复内存快照 |
| ext4 | 同步 CRC16-ANSI group descriptor 校验和驱动打开、关闭流程修复 |
| 构建 | 清理 BDS 与独立 Tools 的旧产物，避免旧对象文件掩盖源码或头文件变化 |

上游曾加入 `fastboot stage` RAM 磁盘功能，随后在 `8da9075` 撤销；本分支沿用撤销后的最终状态，不提供此功能。安装脚本继续使用原有 `persist/efisp`，不会自动迁移到 7.x/B5 存储架构。

## 冲突适配

- **Fastboot 双缓冲**：保留本仓库的 `MaxUSBBufferSize`（总分配）与 `MaxDataBufferSize`（单缓冲）。非 NAND 设备仅公开总容量的一半；下载、fetch、sparse 路径及缓冲指针使用同一容量，避免上游变量重命名覆盖已有越界修复。
- **GPT 表大小**：保留最小分区项数组 `0x4000`，兼容 128 个 128 字节分区项；最大分区数仍为 256。移植 `UpdatePartitionAttributes()` 的 `EFI_STATUS` 返回值及错误传播。
- **README**：保留本仓库已有说明，没有重新加入上游归档徽章。
- **WebUI**：继承主分支的 APatch/FolkPatch 兼容改动；未引入常驻提示、样式变更或额外功能。

## 验证边界

CI 在本测试分支单独运行，不依赖合入主分支。Shell/JavaScript 检查与模拟宿主测试验证 WebUI 协议及语言持久化；GitHub Actions 交叉编译验证 UEFI 和工具源码集成。测试产物与主分支产物按分支和版本区分。

尚需真机验证：旧 `persist/efisp` 启动、容器启动、菜单返回、Fastboot 连接、USB 导出及 GPT 写入失败处理。代码中的内存快照恢复不等同于掉电情况下的磁盘事务保证。

独立的 `boot`/`init_boot` 解除只读脚本按要求单独交付，不纳入仓库或模块包。
