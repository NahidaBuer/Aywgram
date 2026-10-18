# 开发与维护

读者：人类贡献者、维护者与执行相应任务的 Agent。这里维护共享的技术事实；Agent 的授权和操作路由在根 AGENTS.md 及仓库技能中。

- [编码与评审规范](code-style.md)：编辑和评审代码时共同遵循。
- 构建：[Windows x64](building-win-x64.md)、[macOS](building-mac.md)、[Linux](building-linux.md)；[Telegram API 凭据](api_credentials.md)。
- [自动更新与 metadata 发布](aywgram-updater.md)、[发布说明准备](../releases/README.md)。
- [远端 metadata 协议](remote-metadata.md)（[English](remote-metadata.EN.md)）。
- [共享维护工具与测试](../../tools/maintenance/README.md)。
- [CI 缓存](ci-cache.md)（[English](ci-cache.EN.md)）。

构建脚本与依赖版本会随上游变化。以当前配置、`Telegram/build/prepare/`、`Telegram/configure.*` 和 CMake 源码为事实来源，并同步修正文档。输出目录遵循 [out 约定](../agents/out-layout.md)。
