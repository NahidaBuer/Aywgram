# Agent 操作指引

读者：执行仓库任务的 Agent。项目规则以根目录 [AGENTS.md](../../AGENTS.md) 为入口，复用工作流保存在 `.agents/skills/`。

- [共享编码与评审规范](../development/code-style.md)；[API、UI、本地化调用与 RPL](coding.md)，只读取相关主题。
- 构建时只读取目标平台的 [Windows](building-windows.md)、[macOS](building-macos.md) 或 [Linux/WSL](building-linux.md) 指南。
- [长期任务记录](work-tracking.md)：重要任务由 Agent 在 `tasks/` 下持续维护。
- [out 输出与清理约定](out-layout.md)：本地与 CI 共用，区分日志、最终报告、可清理产物与原始证据。

长期计划与结论可以提交；临时计划、原始审计、日志、生成补丁和产物放在 out 对应目录，不提交。checkout 本地脚本放 `.local-tools/`，Python 探测输出放 `.local-tools/results/`。文档不保存私有资料或会话逐字稿。
