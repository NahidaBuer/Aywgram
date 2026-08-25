# AywGram Desktop

[English](README.EN.md)

![Logo 1](Telegram/Resources/art/ayu/nahida/app.png)

AywGram Desktop 是一个基于 [Telegram Desktop](https://github.com/telegramdesktop/tdesktop) 并集成 AyuGram 改动的非官方 Telegram 桌面客户端。本仓库提供完整源代码，供审阅、构建和改进；它不是 Telegram 官方客户端，也不代表 Telegram 或 AyuGram 项目。


## 项目名称迁移说明

本项目并非全新另起炉灶，因此一些内部标识仍然沿用 AyuGram，此为故意为之。截至 7.0 初期版本，本项目的 `tdata` 应该尚可与上游相互替换，但不对后续数据兼容性做出承诺。

如果项目改名导致数据丢失，请手动迁移 `tdata` 目录。详见[迁移应用数据](docs/app-data-migration.md)。

## 功能

- 灵活的幽灵模式、消息历史与防撤回
- 字体、外观与宽屏布局等界面自定义
- 串流模式、本地 Premium 显示、翻译与媒体预览
- WEB 代理：通过内置 WebView 承载的 MTProxy 传输，必要时可由用户手动打开浏览器回退

一些新增的功能，包括但不限于：

- 强化对话设置：缩放贴纸列表、显示媒体元数据、按消息类型筛选搜索等
- 会话迁移：与 Pyrogram 会话串及 Mithka JSON 备份互操作
- 还在缓慢增加中...

~~AywGram 是我的个人项目~~

功能会随上游同步和本仓库的维护策略变化。请只从本仓库的
[Releases](../../releases) 获取与本源码对应的发行包；第三方打包渠道不由本仓库维护。

## 构建与文档

- [构建说明索引](docs/README.md)
- [Windows x64 构建](docs/building-win-x64.md)
- [macOS 构建](docs/building-mac.md)
- [Linux 构建](docs/building-linux.md)
- [迁移应用数据](docs/app-data-migration.md)
- [会话迁移与安全提示](docs/session-backup.md)
- [贡献指南](.github/CONTRIBUTING.md)

构建时使用与 AyuGram 一致的 Telegram 官方 API 标识；详见 [API 凭据说明](docs/api_credentials.md)。
不要把会话文件、`tdata`、代理密钥或构建产物提交到仓库。

## 许可证与致谢

本项目遵循 [GNU GPLv3](LICENSE)，并保留许可证中对 OpenSSL 链接的特别例外。它派生自 Telegram Desktop，并包含 AyuGram 与多个第三方依赖的代码；各组件的版权声明和许可证仍适用于对应文件。详见 [LEGAL](LEGAL) 以及各依赖目录中的许可证文件。

### Telegram 客户端

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
- [64Gram](https://github.com/TDesktop-x64/tdesktop)
- [Yurigram](https://github.com/Revincx/Yurigram)
- IrenaGram

![Logo 2](Telegram/Resources/art/ayu/nahida2/app.png)
