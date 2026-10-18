# AyuGram 修复独立适配与 7.3.1-2 验证

状态：active。

目标：按 2026-10-11 AyuGram 审查结论，在 Telegram 7.3.1 基线上独立适配合适的正确性、安全及兼容修复，保持原子提交，通过 macOS ARM64 Release 编译并打包 7.3.1-2 后推送集成分支。

基线：`b80710ab3e`；工作区开始时干净，沿用 `integrate/ayugram-20261019`。候选范围固定为报告中截至 `d4b7546e77f93ac4c9bb53a3fcf7e9ffbbecf1b5` 的已审阅改动；远端新增改动仅记录，不扩展本次范围。原始报告位于本地 `out/upstream-sync/20261011/ayugram-review.md`，不可作为其他 checkout 的唯一依据。

计划：先处理数据库共享连接、手动已读完成回调、Document TTL 生命周期、macOS Ghost 菜单和翻译能力；再适配 Ghost pending 状态、链接安全与插件解析、发送/转发、限时媒体及其他 UI 修复。逐项记录采用、已覆盖或暂缓的理由。保留自有更新器、防截图、品牌、设置和异步发送生命周期，不导入上游工作流。

验证：每个提交进行 diff/冲突检查和针对性静态检查，最终执行完整本地 Release 编译、归档架构/签名与版本校验。尚未执行编译或账号运行测试。构建产物与原始证据仅置于 out/，复用本地工具仅置于 .local-tools/。

发布：使用 `pre-release-v7.3.1-2` 的发行身份；提交源保持 revision 0，编译期间临时注入 revision 2。推送集成分支在用户授权范围内；不发布 GitHub Release 或线上更新元数据。
