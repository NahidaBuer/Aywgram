# AywGram 发布说明

读者：用户、贡献者和准备发行的 Agent。每个完整版本对应 `<base>-<revision>.md`，正文中英双语；这些文件是发布内容源，根目录 [changelog.txt](../../changelog.txt) 由其中的 Ayw 部分生成。发布流程从下一版开始采用，不补录历史版本。

每份说明包含一个单行 `<!-- ayw-release {JSON} -->` 元数据注释：`tag`、`previous_tag`、`reviewed_commit`、`date`（ISO 日期），以及 `upstreams.ayu` 和 `upstreams.telegram` 各自的 `from`、`to` 完整提交 SHA 和 `url` 证据链接。两个上游均应记录基线；没有更新时 from 与 to 相同，并在正文明确说明没有新增上游改动。

正文使用 `# AywGram <完整版本>` 标题，依次包含二级标题 `AywGram`、`Contributors / 贡献者`、`AyuGram`、`Telegram Desktop`。除贡献者外，每节都有三级标题 `简体中文` 和 `English` 及其摘要；Ayw 节可在各语言内按新增、改进、修复和已知问题分组。贡献者注明本次贡献与可核实的来源，不把整个上游历史的作者都列为 Ayw 贡献者。

元数据中的审阅提交是发布准备前最后一个包含实质改动的提交；此后的发布准备提交只修改本版说明和生成的根 changelog。先完成其他文档和任务记录，再固定审阅基线，避免摘要遗漏。未来需要纠错时修改内容源并重新生成 changelog；已经发布的源码 tag 不移动。

使用仓库的 `aywgram-release` skill 准备说明。确定性检查：`python tools/maintenance/release_notes.py changelog --check`；发版前对已提交源码运行 `python tools/maintenance/release_notes.py validate --tag <tag>`。检查通过只表示结构和范围一致，不证明文字准确、翻译质量或产品行为。
