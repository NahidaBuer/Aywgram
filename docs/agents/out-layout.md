# out 目录约定

读者：Agent、构建脚本与维护工具的作者。本文件定义本地和 CI 共用的输出边界；平台构建命令仍以对应指南为准。`out/` 整体不提交。

| 路径 | 内容 | 清理类别 |
| --- | --- | --- |
| `out/build/<target>/<configuration>/<profile>/` | CMake/Xcode/Ninja 工作树及原生链接输出 | `intermediates` |
| `out/artifacts/<target>/<configuration>/<run-id>/` | 从成功构建复制的完整程序、Updater、modules 或 app bundle；打包在此副本上处理 | `binaries` |
| `out/dist/` | 完整版本号命名的最终 ZIP、tar.gz | `packages` |
| `out/metadata/<tag-or-run-id>/` | metadata fragments、合并输入和发布说明预览 | `evidence` |
| `out/logs/<workflow>/<run-id>/` | 执行日志、最终报告、发行身份与校验结果 | 保留 |
| `out/upstream-sync/<run-id>/logs/` | 上游审计、合并与验证日志 | 保留 |
| `out/upstream-sync/<run-id>/report.md` | 最终维护结论 | 保留 |
| `out/upstream-sync/<run-id>/evidence/` | 提交清单、审计数据、diff 和临时补丁 | `evidence` |
| `out/upstream-sync/<run-id>/tmp/` | 上游维护临时工作文件 | `temporary` |
| `out/plans/<run-id>/` | 临时计划草稿；长期计划转入任务记录 | `evidence` |
| `out/tmp/<workflow>/<run-id>/` | 下载解压、打包及测试 scratch | `temporary` |
| `out/cache/compiler/<target>/` | 可重建的 ccache/sccache 缓存 | `compiler-cache` |
| `out/cache/downloads/` | 可重新获取的下载缓存 | `downloads` |
| `out/dependencies/src/` | 本地补充依赖源码 checkout、补丁源码 | 保留 |
| `out/dependencies/build/<target>/` | 补充依赖编译输出，如 Cargo target | `intermediates` |
| `out/tools/` | CI 工具和 wrapper；不存个人配置 | 保留 |

`target` 使用 `windows-x86_64`、`windows-arm64`、`mac-arm64`、`mac-x86_64`、`mac-universal` 或 `linux-x86_64`；`configuration` 使用 `Release` 或明确授权的 `Debug`。`profile` 默认 `default`；需要并存工具链时使用简短名称，并验证实际 CMake cache，目录名不代表工具链已经通过验证。目录时间标识默认使用 UTC，面向用户报告日期时采用用户时区。`run-id` 使用 `YYYYMMDD-HHMMSS-主题`，CI 使用 `ci-<run-id>-<attempt>-<target>`。

通过配置命令的显式 `-B` 参数（Docker 入口使用 `AYW_BUILD_DIR` 指定容器内路径）选择工作树，不在 `out/` 根目录直接配置。生成器在工作树内的 `Release/`、`bin/` 和其他内部布局无需强行改写。多架构 macOS 构建为各架构使用独立工作树，合并后的程序放进 artifacts。

`intermediates` 会删除工作树内的原生程序副本；需要保留或运行的成功产物必须先完整复制到 artifacts，再从 artifacts 打包到 dist。这样清理中间产物不会删除保留的程序和发行包；下次构建可能需要重新配置和完整编译。不得把真实 `tdata` 或日常运行数据放在这些可清理目录。

构建开始前创建 `out/logs/build/<run-id>/`，保存 configure、build、package 日志；最后保存 `report.md` 和机器可读的版本、源码、工作树、程序路径及归档校验结果。失败也保留已有日志。原生工具额外产生的 `.log`、`.xcactivitylog` 和 `CMakeConfigureLog.yaml` 在清理前保存在 cleanup 日志目录，并保留原相对路径。

日志和最终报告不作为清理选项。原始审计数据、临时补丁、计划草稿及其他 evidence 可单独选择删除；尚未提交的源码修改、唯一恢复副本或仍需实施的补丁应先保全，不因位于 evidence 而自动丢弃。依赖源码、Cargo home 中的配置/凭据和仓库外 `Libraries` 不属于清理范围。

清理 skill 只认识本规范中的目录，不扫描、推断或迁移旧布局。未知目录原样保留，旧内容单独人工审阅处理。所有清理先生成包含类别、具体路径、大小和快照的计划，再执行已经获准的类别；明确的清理请求无需重复确认同一范围。没有指定类别时先展示盘点并询问选择。
