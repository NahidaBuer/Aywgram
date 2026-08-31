# 文档索引

这里收录普通用户需要的迁移说明，以及面向构建者和贡献者的构建与维护资料。

## 构建

- [Windows x64](building-win-x64.md)
- [macOS](building-mac.md)
- [Linux](building-linux.md)
- Agent 构建规则：[Windows](agents/building-windows.md)、[macOS](agents/building-macos.md)、[Linux](agents/building-linux.md)
- [Telegram API 凭据](api_credentials.md)

构建脚本和依赖版本会随 Telegram Desktop 上游变化。若文档与脚本不一致，以 `Telegram/build/prepare/`、`Telegram/configure.*` 和本仓库的 CMake 配置为准，并在提交中一并更新文档。

## 功能与维护资料

- [迁移应用数据](app-data-migration.md)：Windows、macOS 与 Linux 的默认数据路径、便携模式和迁移注意事项。
- [会话迁移](session-backup.md)：Pyrogram 会话串与 Mithka JSON 的格式、安全限制和互操作范围。
- [自动更新与 metadata 发布](aywgram-updater.md)：GitHub Release metadata、发行资产布局、客户端安装流程和维护政策。

不在此目录保存个人维护记录、私有远程地址、测试账户资料或部署凭据。
