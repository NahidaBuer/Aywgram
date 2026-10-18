# AyuGram 修复独立适配与 7.3.1-2 验证

状态：completed。

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

构建环境确认：Xcode 26.5（17F42）、Qt 6.11.2、ARM64，使用新 out/build/mac-arm64/Release/default，不复用已清理的旧工作树。必须显式选择 Xcode 的 macOS 26.5 SDK；系统默认 xcrun 的 SDK 路径指向不兼容的 CommandLineTools 27.0 SDK。链接保留本机 libheif 的 libde265 静态依赖。完整应用 Release 编译与 7.3.1-2 打包验证均已完成；CMake 公共缓存的部署目标为 10.13，实际 ARM64 Mach-O 的最低系统为 11.0。

## 交付结论

完整 macOS ARM64 Release 编译通过；构建暴露并修复了 GIF 视图缺少 AyuSettings 头文件、TTLButton 包装类缺少 isHidden 接口两处兼容问题。最终链接规则审查还补齐了自定义协议使用受信任域名时的确认要求。

7.3.1-2 打包探针通过，主程序、Updater 与 Swift Concurrency 三个 Mach-O 均为单一 ARM64；去除只含 x86_64 且 ARM64 加载路径不使用的 Swift Core，再对完整副本 ad hoc 重签名。应用副本与 ZIP 解压副本的深度严格签名验证、架构扫描和 metadata fragment 校验全部通过。原生未处理 bundle、首次失败的打包副本和全部日志保留在 out/。打包工具按目标架构检查 dylib 依赖，避免把通用库中其他架构的依赖误判为 ARM64 需求。

整理未推送历史，将两处编译修复和链接确认修复合入各自功能提交，并将发布准备合为一个最终提交。最终标签身份的构建、保留程序、ZIP、摘要和远端分支核对以本地 `out/logs/build/20261019-091000-ayugram-7.3.1-2/report.md` 与 `out/upstream-sync/20261019/report.md` 为准；它们记录确定的最终提交与校验结果。源代码保留 revision 0，构建期间注入 revision 2，退出时仅恢复此临时差异。原有本地 rlottie 目录与所有其他用户内容保留。

验证边界：没有运行应用、登录账户或执行真实消息/UI 回归；没有验证 Windows/Linux 编译。本次仅推送集成分支，不发布 GitHub Release、远端版本标签或线上更新元数据。后续应覆盖多账户 Ghost、网络失败、限时媒体查看/销毁、异步转发取消与 rich File/ButtonRow、外部翻译及链接交互。
