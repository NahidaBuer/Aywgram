# CI 依赖缓存误删钱包绑定源码

状态：completed

## 目标与复现

修复 [Actions run 38905573115](https://github.com/NahidaBuer/Aywgram/actions/runs/38905573115) 的 macOS ARM64 与 Windows ARM64 配置失败，保留钱包功能和现有缓存流程。该运行使用提交 `8af1f0e886711a86ac816e2c2304c8ffa8bb6fe7`；Linux 构建成功。

## 根因与决策

- `tdesktop_rust` 阶段成功生成钱包 C++ 绑定，但随后工作流的依赖缓存裁剪仅保留头文件与库，删除了 macOS `local/src/wallet_engine/wallet_engine.cpp` 和 Windows `tdesktop_rust/out/src/wallet_engine/wallet_engine.cpp`，CMake 因此报找不到源码。
- 裁剪还会删除 `tlottie` 与 `wallet-engine` 的 Rust 源码和 Cargo 清单，而对应阶段的缓存标记继续有效，导致后续 Rust 重建也可能失败。
- 保留生成的绑定源码及上述两个源码 checkout；更新三个相关准备阶段的版本，让已有残缺缓存重新准备。只更新依赖阶段，不删除整个缓存或变更钱包功能。

## 执行计划

- 使用两平台实际工作流中的裁剪命令，在临时目录复现文件误删并增加回归验证。
- 更新保留规则和阶段版本，运行受影响的缓存测试、工作流语法及静态检查。
- 记录验证边界与待验证事项；本任务未授权本地应用编译、推送或发布。

## 验证与后续

已读取两个失败 job 的完整日志，确认生成、裁剪与配置错误的顺序。图标更新与移除已另行提交为 `d1f76145d0`，本记录仅跟踪 CI 修复。

- 两项回归测试直接执行工作流中的裁剪命令：原规则均失败，修复后均通过；生成绑定、Rust 源码、Cargo 清单、Git 元数据、静态库与缓存标记保留，无关中间文件仍被移除。
- 现有 15 项缓存测试通过。使用实际准备脚本的阶段过滤与缓存键函数，验证 macOS、Windows ARM64 的三个相关阶段共 6 个组合：旧标记变为 `Stale`，更新后的标记为 `Good`；依赖版本与源码修订没有变化。
- `actionlint` 检查两平台工作流通过；修改的 Python 文件通过语法检查；`git diff --check` 通过。
- 未执行本地应用构建、推送、线上 CI 重跑或发布；完整 macOS/Windows 构建仍需在后续授权运行中确认。CI 修复留在工作区，未包含在图标提交中。
