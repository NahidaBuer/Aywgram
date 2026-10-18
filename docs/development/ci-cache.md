# CI 缓存

[English](ci-cache.EN.md)

Windows、macOS 和 Linux 工作流共用 `ayw-ci-v3` 缓存命名空间，推送发布 tag 后仍会自动发布预发布版本。所有支持的目标默认启用应用编译缓存，包括本次修复的 macOS ARM64 和 Windows ARM64 路径。这两处修复仍需在合并前完成冷、热 CI 构建验证。所有平台始终保留依赖缓存。

## 推送 tag 与共享缓存

发版操作保持不变：本地创建并推送 `pre-release-v<base>-<revision>` tag。tag 触发的工作流只负责转发，在默认分支（当前为 `main`）上启动同一个 Prerelease 工作流；实际构建 checkout 的仍是 tag 对应的固定 commit SHA。工作流运行 ref 决定缓存作用域，因此这些发布构建与 main 上的预热可以读取、更新同一组兼容缓存，无需在每次发版前手动预热。

自动转发同时传入 `release_tag` 和完整 `source_sha`，兼容轻量 tag 与附注 tag。准备阶段检查 tag 是否仍指向该提交，再校验源码版本，成功后才创建或更新 Release。各平台和元数据任务都使用同一个 SHA，不会因 main 后续更新而改用其他源码。不要在发布过程中移动或删除 tag。

Actions 中会出现两条记录：tag 上的转发运行，以及 main 上的实际构建运行。转发摘要提供实际构建链接；转发成功只表示 GitHub 接受了请求，构建与发布是否成功以链接中的运行结果为准。转发失败或响应不明确时不会自动重试，请先检查 Actions 中是否已有实际构建，再决定是否重试。转发任务使用具有 `actions: write` 的 `GITHUB_TOKEN`，无需新增 PAT；发布任务继续使用原有凭据。

手动重跑完整发布流程时，在 **Actions → Prerelease → Run workflow** 中选择 `main`，填写已有的 `release_tag`；可选的 `source_sha` 留空时自动解析，填写时必须是与 tag 一致的完整 commit SHA。完整发布入口拒绝从非默认分支运行。单个平台的直接手动入口仍可使用；若希望其更新共享缓存，也应选择 main 并通过 `release_tag` 指定源码。转发与实际构建使用不同并发组，同一 tag 的重复实际构建仍会取消前一次运行，不同 tag 互不取消。

启用时先将改动合入 main，再从包含新工作流和辅助脚本的提交创建新 tag。历史 tag 中的旧工作流不会自动获得转发逻辑。首次运行可能是冷构建；后续发布能否命中仍取决于兼容缓存是否存在、工具链和依赖是否变化，以及缓存是否被淘汰。默认平台、文件命名、Release、更新元数据和 Telegram 通知流程保持不变，Windows x64 仍通过原有手动流程提供。

## 手动预热

在 **Actions → Warm CI caches → Run workflow** 中选择 `main` 分支运行。工作流构建本次触发对应的 main 提交，包括应用本身，并保存缓存；不会打包、上传产物、修改 Release 或发送 Telegram 通知。预热没有定时触发器，也不会随 main 的推送自动运行。

默认目标 `all` 包含 Linux x86_64、macOS ARM64 和 Windows ARM64，也可以单独选择其中一个目标。Windows x86_64 可单独预热，仍不参与自动预发布构建。同一平台、同一架构的预热共用并发组，并设置 `cancel-in-progress: false`。多个请求排队时，GitHub 可能用新请求替换较早的待运行请求，但不会因此取消已经开始的预热。

可复用的平台工作流默认接收 `warm_cache=false`、各平台自己的 `compiler_cache` 默认值，并通过 `workflow_call` 接收可选的 `source_sha`。macOS、Linux 和两种 Windows 架构的编译缓存默认开启；设置 `compiler_cache=false` 可以绕过应用编译缓存。预热必须在默认分支运行，`release_tag` 与 `source_sha` 必须为空，且 `distribute` 与 `update_metadata` 都必须关闭。直接手动运行平台工作流也支持预热，并在可用平台提供编译缓存开关。

手动预热适合首次发版前、依赖或工具链更新后，以及长时间未构建后使用；日常发布本身也会更新 main 上的缓存。推送 tag 不会等待正在进行的预热。旧 tag 作用域中的缓存不会迁移到 main，也不会被主动删除。超过七天未访问的缓存可能过期，旧快照也会占用仓库缓存配额。详见 [GitHub 的缓存作用域与淘汰规则](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching)。

## 缓存层次

| 平台 | 依赖缓存 | 应用编译缓存 |
| --- | --- | --- |
| Windows ARM64 | ThirdParty、非 Qt 的 Libraries、Qt 分开存储，各自包含对应的依赖准备阶段标记 | sccache 0.17.0，使用本地磁盘，上限 1 GiB；待 CI 验证 |
| Windows x64 | ThirdParty、非 Qt 的 Libraries、Qt 分开存储，各自包含对应的依赖准备阶段标记 | sccache 0.17.0，使用本地磁盘，上限 1 GiB |
| macOS ARM64 | Libraries 与 ThirdParty 合并存储 | ccache 4.13.6，通过 CMake 和 Xcode 编译器包装器接入，上限 1 GiB；待 CI 验证 |
| Linux x86_64 | Docker BuildKit 构建层与 cache mounts 分开存储 | ccache，使用本地磁盘，上限 1 GiB；容器运行时显式覆盖容量设置 |

缓存 key 包含平台、目标架构和工具链指纹。Windows 记录实际使用的 MSVC、Windows SDK 与 CMake 版本；macOS 记录 Xcode、SDK、AppleClang 与 CMake 版本；Linux 记录生成后的 Docker 构建上下文指纹。依赖哈希覆盖 Git 跟踪的 prepare 脚本及 `qt_version.py`，或 Dockerfile 生成后的受跟踪构建上下文文件。临时文件、Python 字节码和虚拟环境不参与哈希。

Windows 和 macOS 的依赖回退恢复范围限制在同一工具链内。恢复后，prepare 脚本仍会逐个检查依赖阶段；仅仅命中 GitHub Actions 缓存不会跳过这些检查。Windows x64 使用 `Libraries/win64`，ARM64 使用 `Libraries`。在支持的目标上，应用编译缓存的恢复前缀还包含依赖指纹、缓存工具版本与 Release 配置；每次保存时追加运行 ID 和重试次数，使快照具有独立 key，同时允许后续运行恢复先前的快照。普通应用源码变动不会改变恢复前缀。

Windows CI 使用 Ninja Multi-Config、选定的 Native Tools 环境和现有 Release 设置，本地 Visual Studio 构建不受影响。两种 Windows 架构都可以使用 sccache。仅在依赖准备和缓存保存完成后安装，避免 Meson 在依赖构建时自动启动应用缓存服务。使用 `sccache --zero-stats` 重置统计；该命令会连接已有服务，或在没有服务时启动。MSVC 的预编译头（PCH）保持启用。sccache 会绕过使用 PCH 的编译，因此缓存收益主要来自其余编译任务。分析整体命中率时，应同时查看不可缓存请求的计数。

macOS 在同一个 runner 上准备依赖并构建应用，全程使用同一套 Xcode。继续使用 Xcode generator，以保留资源目录、图标和 app bundle 处理。依赖准备完成后才安装 ccache。启用 `compiler_cache=true` 时，CMake 编译器设置和 Xcode 的 CC/CXX 设置使用同一组包装器，覆盖 AutoMoc 生成预定义宏时的编译器调用。包装器清除非 macOS 平台的 deployment target 环境变量，保留 `MACOSX_DEPLOYMENT_TARGET`、SDK 和编译参数。链接器设置仍指向真实 clang/clang++。缓存不包含整个 Xcode 构建目录或最终应用 bundle。

Linux 在构建依赖镜像前注入缓存 mounts，并在保存 mount 压缩包前显式导出。导出使用固定提交版本的 cache-dance CLI，作为普通步骤运行，不再依赖 job 结束时的 action post 步骤。导出的依赖 ccache 在上传前裁剪至 1 GiB，包管理器的 mount 数据保留。导出失败时，仅跳过 mount 缓存保存，失败会体现在步骤结果中；已完成的 Docker 构建层仍可保存并用于构建应用。依赖缓存会在应用编译之前保存，因此后续应用编译失败不会丢失已经准备好的依赖。应用缓存出现少量新增内容时也会保存，取消的运行则不保存应用缓存。

编译缓存工具使用固定的官方发布版本，下载后校验仓库内记录的 SHA-256 摘要。升级工具时，必须同步更新下载产物、摘要和缓存 key 中的工具版本。这些工作流不会主动删除现存远端缓存，也不会修改仓库存储配额或计费设置。

## 查看结果与验证改动

每次构建都会在运行摘要中记录工作流 ref（缓存作用域）、发布 tag、实际源码 SHA、实际工具链、请求和恢复的 key、精确命中或回退命中状态、依赖与应用耗时、编译缓存计数（包括不可缓存原因）、本地缓存大小，以及 API 可访问时的仓库缓存总占用。统计读取失败会明确显示为不可用，并阻止保存该次应用缓存快照，不会将失败伪装成零 miss。缓存统计或摘要失败不会把成功的应用构建变成失败。

实现后，应分别在全新 runner 上验证三个自动发布平台的冷、热 Release 构建，并单独验证 Windows x64。在支持编译缓存的目标上，确认编译器实际经过缓存包装并出现命中；在所有目标上确认依赖阶段正确跳过，并检查目标架构、Updater、macOS 图标与 bundle 内容，以及现有发行包布局。获准发布后，用两个不同 tag 验证实际构建均在 main 上运行，源码 SHA 分别对应各自 tag，且第二次能恢复第一次保存的兼容缓存。不能仅凭缓存目录有内容或恢复步骤显示成功，就认定编译成功。

静态检查包括：对平台和预热工作流运行带 ShellCheck 的 `actionlint`，以及执行 `python -B -m unittest discover -s .github/scripts -p 'test_*.py'`。CI 构建和发布需在获得相应授权后运行。缓存布局不兼容时，通过升级命名空间使旧缓存失效；不要默认通过删除共享缓存来排查问题。
