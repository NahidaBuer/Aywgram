# 文档、发布与 out 工作流规范化

状态：completed

## 目标

按用户、贡献者与 Agent 区分仓库文档；为重要工作保存长期记录；建立中英双语的 Ayw 发布说明与根 changelog；统一本地和 CI 输出目录，并提供可选择类别、保留日志与最终报告的清理 skill。

## 决策

- 重要任务一份记录，小改动使用 Git 历史。根 `changelog.txt` 改为 Ayw 专属，上游日志独立保存。从下一版开始，不补录历史发行。
- Release 按 Ayw、贡献者、Ayu、Telegram 排列，CI 信息置后。没有摘要的版本在构建前失败，不兼容旧 tag 重跑。
- 固定 metadata Release 的每个平台链接实际版本的发行说明。
- 清理工具只支持新目录约定，旧布局人工处理。日志与最终报告保留，原始证据可选择清理。现有补充依赖 checkout 有本地修改，保留原位。
- 共享工具及其测试归入 tools/maintenance/ 与 tests/；.github/scripts/ 只保存 CI 专用入口。
- 用户明确选择人工删除旧构建树、根目录生成物及未打包的 app，保留发行包、依赖源码和日志报告。

## 执行计划

- 重组文档并修复路由及链接。
- 增加 release、clean-out 技能及确定性辅助脚本。
- 更新本地构建指南、现有技能与 CI 的目录和发布流程。
- 在隔离临时目录测试发布校验、正文拼装和选择性清理；执行受影响的静态检查。

## 验证与后续

已完成文档迁移、两项新技能、构建技能/平台指南、共享工具与 CI 改造。执行 75 项 Python 测试，并通过 actionlint、Python/Bash 语法、文档链接与代码围栏、技能结构和 git diff 检查。官方 changelog 副本与迁移前原文件逐字节一致。

按用户选择人工删除 17 项旧构建输出，涉及 9,054,878,853 文件字节；保存 6 份原生日志/配置快照。out 占用从约 9.8 GiB 降至 1.3 GiB。清理报告在本地 `out/logs/cleanup/20261018-192627-legacy/report.md`（目录标识使用 UTC）。既有发行压缩包、补充依赖源码修改、根日志、local-build 和 upstream-sync 报告保留。

未执行应用编译、实际 Actions 构建、线上 Release 修改或推送；按用户后续请求将本次改动提交到本地。PowerShell 审计入口仅作静态检查，当前环境未安装 pwsh；Windows/macOS/Linux 新构建路径仍需下次授权构建验证。保留原有未跟踪的 `Telegram/ThirdParty/rlottie/`。
