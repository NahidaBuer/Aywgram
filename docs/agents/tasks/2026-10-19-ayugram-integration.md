# AyuGram 修复独立适配与 7.3.1-2 验证

状态：active。

目标：按 2026-10-11 AyuGram 审查结论，在 Telegram 7.3.1 基线上独立适配合适的正确性、安全及兼容修复，保持原子提交，通过 macOS ARM64 Release 编译并打包 7.3.1-2 后推送集成分支。

基线：`b80710ab3e`；工作区开始时干净，沿用 `integrate/ayugram-20261019`。候选范围固定为报告中截至 `d4b7546e77f93ac4c9bb53a3fcf7e9ffbbecf1b5` 的已审阅改动；远端新增改动仅记录，不扩展本次范围。原始报告位于本地 `out/upstream-sync/20261011/ayugram-review.md`，不可作为其他 checkout 的唯一依据。

计划：先处理数据库共享连接、手动已读完成回调、Document TTL 生命周期、macOS Ghost 菜单和翻译能力；再适配 Ghost pending 状态、链接安全与插件解析、发送/转发、限时媒体及其他 UI 修复。逐项记录采用、已覆盖或暂缓的理由。保留自有更新器、防截图、品牌、设置和异步发送生命周期，不导入上游工作流。

验证：每个提交进行 diff/冲突检查和针对性静态检查，最终执行完整本地 Release 编译、归档架构/签名与版本校验。尚未执行编译或账号运行测试。构建产物与原始证据仅置于 out/，复用本地工具仅置于 .local-tools/。

发布：使用 `pre-release-v7.3.1-2` 的发行身份；提交源保持 revision 0，编译期间临时注入 revision 2。推送集成分支在用户授权范围内；不发布 GitHub Release 或线上更新元数据。

## 集成决策与验证进度

已独立实现：共享 ORM 全入口互斥及读取异常回退；手动已读成功/失败收尾；Ghost 仅清待发送状态；Document TTL 动画与对象同寿命且防止 lifetime 已销毁后的绘制访问；外部翻译文本能力；macOS Ghost 菜单缓存；危险外部协议限制、HTTP(S) 警告开关边界及插件 Markdown 目的地限制；ephemeral 发送与 Ghost 调度隔离；Saved Music 伪回复和 premium emoji 规范化；真实 Ayu noforwards；异步 RichForward 的 File remap、按钮筛选及文本清理；保留 TTL 媒体烧毁后的组件清理与视图刷新；发送场景确认、TTL 控件可见性、欢迎消息截图以及 WebView 平台/尺寸。

插件多行字符串、括号拼接、描述链接/粗体在实际基线中已经存在，原调查对此已过时。本次改为逐字符解析字符串转义，拒绝不完整括号和表达式后缀，避免替换顺序损坏字面量反斜杠。纯外观的圆形 TTL 图标坐标调整暂缓；未恢复同步 RichForward，未关闭防截图，未替换更新器、数量滑块、品牌或工作流。未扩大到新的 Telegram 或 supplemental 分支改动。

本地边界探针使用实际 Qt 6.11.2 编译提取后的生产 URL/metadata 函数，覆盖危险协议、tg/ton/tonsite/Spotify/mailto、大小写、空字符串、单双/三引号、拼接、转义及坏括号，全部通过。数据库探针直接编译生产 ayu_database.cpp，在隔离目录用 8 个线程完成 800 次编辑和删除记录写入、并发读取、LIKE 转义、删除与缺表读取异常回退，全部通过。探针源码和原始结果仅在 .local-tools/；不读取真实 tdata。

静态检查：无冲突、diff --check 通过，i18n 完整审计通过，626 键/24 locale 契约一致；未修改语言键。历史翻译缺口保留为专门维护范围。修复了前一提交意外纳入、但 .gitmodules 已无配置且构建不再使用的 rlottie gitlink；原本地目录保留，以 .git/info/exclude 隐藏。递归子模块状态恢复正常，其余 gitlink 未变。

构建环境确认：Xcode 26.5（17F42）、Qt 6.11.2、ARM64，使用新 out/build/mac-arm64/Release/default，不复用已清理的旧工作树。必须显式选择 Xcode 的 macOS 26.5 SDK；系统默认 xcrun 的 SDK 路径指向不兼容的 CommandLineTools 27.0 SDK。链接保留本机 libheif 的 libde265 静态依赖。完整应用 Release 编译与 7.3.1-2 打包尚待完成。
