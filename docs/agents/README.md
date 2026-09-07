# Agent 操作指引

本目录保存按任务读取的项目操作指引，不存放计划、日志、生成物或 Skill 执行输出。

- 编码参考：[API、UI、本地化调用与 RPL](coding.md)。只读取当前修改涉及的主题；编码风格规则在 [REVIEW.md](../../REVIEW.md)。
- 构建与工具链：只读取目标平台的 [Windows](building-windows.md)、[macOS](building-macos.md) 或 [Linux/WSL](building-linux.md) 指南。
- 项目规则及维护技能入口以根目录 [AGENTS.md](../../AGENTS.md) 为准。

构建产物、维护报告、计划和日志放在仓库根目录 `out/`。可复用的本地工具放在 `.local-tools/`，其探测结果放在 `.local-tools/results/`，均不提交。仓库维护技能及其参考资料保存在 `.agents/skills/`；不要将普通任务记录写入本目录或技能目录。
