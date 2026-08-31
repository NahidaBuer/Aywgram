# AywGram 自动更新机制

AywGram Desktop 的内置更新器只读取 GitHub Releases 中的 JSON metadata。客户端不会访问 `api.github.com`，唯一入口是：

```text
https://github.com/NahidaBuer/AywGram/releases/latest/download/update-metadata.json
```

仓库长期只保留一个已发布、非 draft、非 prerelease 的普通 Release。它是永久的 metadata 载体，只允许包含 `update-metadata.json`，不承载程序归档，也不随应用版本创建或改名。因为它是仓库唯一的普通 Release，GitHub 的 `latest` URL 始终指向它；Release 正文由同一份 metadata 自动生成，提供当前受支持平台的版本和归档直链。

应用归档全部位于 `pre-release-v<base>-<revision>` prerelease，例如 `pre-release-v7.1.3-1`。prerelease 不会转为普通 Release；一次成功的 prerelease 构建会把成功平台写入 metadata，因此可立即成为对应平台的自动更新来源。

`base` 必须与源码中的 `AppVersionStr` 完全一致，可以是 `7.2` 或 `7.2.1`；`revision` 是无前导零的 `1..99`。源码中的 `AppVersion` 和 `AppVersionStr` 始终表示上游基础版本，`AppReleaseRevision` 在普通本地构建中为 `0`，prerelease CI 根据 tag 临时注入。用户可见版本和归档名使用完整的 `<base>-<revision>`，例如 `7.1.3-1`。

## Metadata schema v1

```json
{
  "schema": 1,
  "feed_release": "update-metadata",
  "generated_at": "2026-09-01T12:00:00Z",
  "targets": {
    "windows-x86_64": {
      "app_version": 7001003,
      "revision": 1,
      "version_name": "7.1.3-1",
      "release": "pre-release-v7.1.3-1",
      "url": "https://github.com/NahidaBuer/AywGram/releases/download/pre-release-v7.1.3-1/AywGram-v7.1.3-1-windows-x86_64.zip",
      "format": "zip",
      "size": 123456789,
      "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    }
  }
}
```

`feed_release` 是唯一 metadata Release 的 tag；每个 target 的 `release` 是实际承载归档的 prerelease tag。两者通常不同。tag 只接受字母、数字、点、下划线和连字符，且必须以字母或数字开头。

固定目标键为：

- `windows-x86_64`
- `windows-arm64`
- `mac-arm64`
- `linux-x86_64`

各目标的版本独立滚动。版本按 `(app_version, revision)` 字典序比较；基础版本升级时 revision 可以从 `1` 重新开始。新的 prerelease 缺少某个平台时，合并器保留该平台最近一次成功记录，所以同一份 metadata 可以指向多个 prerelease。合并器拒绝版本回退；相同二元版本重建只允许沿用相同 prerelease tag 和资产名。

客户端忽略未知字段，但拒绝未知 schema、缺失或非法的 `app_version`、`revision`、`version_name`、超过 64 KiB 的 metadata、非正整数大小、超过 1 GiB 的归档、非 64 位小写 SHA-256、未知格式，以及不属于 `https://github.com/NahidaBuer/AywGram/releases/download/` 的 URL。`version_name` 必须等于基础版本和 revision 的组合，`release` 必须等于 `pre-release-v{version_name}`。schema v1 只接受 `zip` 和 `tar.gz`，不兼容开发期间使用过的单一 `version` 字段。

## Prerelease 资产

自动更新复用普通用户下载的发行归档，不生成 `.aywupd` 或其他专用更新包。

| 目标 | 文件名 | 格式 | 根目录必需内容 |
| --- | --- | --- | --- |
| Windows x86_64 | `AywGram-v{version_name}-windows-x86_64.zip` | ZIP | `AywGram.exe`、`Updater.exe` 和所需模块 |
| Windows ARM64 | `AywGram-v{version_name}-windows-arm64.zip` | ZIP | `AywGram.exe`、`Updater.exe` 和所需模块 |
| macOS ARM64 | `AywGram-v{version_name}-mac-arm64.zip` | ZIP | `AywGram.app` |
| Linux x86_64 | `AywGram-v{version_name}-linux-x86_64.tar.gz` | tar.gz | `AywGram`、`Updater` |

Linux 的两个程序必须带执行位。macOS bundle 中的 `Contents/MacOS/AywGram` 和 `Contents/Frameworks/Updater` 必须存在且可执行。macOS 归档使用 `ditto -c -k --sequesterRsrc --keepParent` 创建，以保留 bundle 权限、符号链接和扩展属性。Windows 的 `.exe` 安装器不属于 schema v1 的自动更新资产；手动上传时仍需提供符合上述命名和布局的 ZIP。

macOS 自动更新和 Actions 构建仅支持 ARM64。

## 客户端流程

1. 按检查周期请求固定 metadata URL，并附加周期 cache-buster、`no-cache` 和强制网络重新验证设置。
2. 根据当前平台和架构选择目标，按 `(app_version, revision)` 与当前 `(AppVersion, AppReleaseRevision)` 比较。目标缺失或远端二元版本不更高时进入 Latest 状态；同 tag 重建不会使已安装相同版本的客户端重复更新。
3. 使用 HTTPS 下载目标资产，并保留 HTTP Range 断点续传。
4. 流式核对实际大小和 SHA-256。摘要只检测下载损坏，不构成独立的发布签名。
5. 在全新的 `tupdates/temp` 中解压：Windows 使用仓库已有的 minizip；macOS 先校验 ZIP 成员，再以 `/usr/bin/ditto -x -k` 解压；Linux 通过 `QProcess` 直接调用系统 `tar`，先列出成员，再以 `--no-same-owner` 解压，不经过 shell。
6. 所有平台拒绝绝对路径、`..`、盘符或反斜杠逃逸、归档内伪造的 `ready`、`tdata` 和 staging metadata。展开总量不得超过 4 GiB，并且只接受预期布局和文件类型。
7. 客户端验证布局后才生成 `tdata/version`、schema v1 staging manifest 和最后的 `ready` 标记。`tdata/version` 只记录基础 `app_version`，staging manifest 额外记录 `revision` 和完整 `version_name`。任何校验或解压失败都会清除下载与暂存目录，当前版本继续运行并在之后重试。
8. 重启时 `checkReadyUpdate()` 只接受带新 manifest 的暂存目录。独立 Updater 继续负责退出后的原子替换、便携模式、Windows 写保护处理和 macOS bundle 替换。

Flatpak、Snap 和商店构建仍使用各自平台的更新渠道。

## Actions 拓扑

唯一普通 Release 需要先在 GitHub 上创建并发布一次。它的 tag 和标题可以自行选择，但此后不要删除、重建或改名，并确保其中除 `update-metadata.json` 外没有其他资产。两个 metadata 写入路径都会检查仓库中恰好只有这一个普通 Release，否则停止而不覆盖任何内容。

### 自动 prerelease 构建

将严格格式的 tag，例如 `pre-release-v7.1.3-1`，推送到仓库后，[`.github/workflows/pre-release.yml`](../.github/workflows/pre-release.yml) 创建或更新同名 prerelease，并调用 Linux、macOS ARM64 和 Windows ARM64 的复用构建工作流。tag 的 base 必须与所指向源码的 `AppVersionStr` 完全一致，且按现有公式计算出的数值必须等于 `AppVersion`，否则工作流在创建 Release 前失败。

1. 平台工作流验证归档布局、Updater、文件类型和权限。
2. 归档上传到当前 prerelease。
3. 每个成功平台输出 metadata fragment，其中包含归档大小和 SHA-256。
4. metadata job 下载本次成功 fragments，读取唯一普通 Release 中的旧 metadata，滚动合并后覆盖该 Release 的 `update-metadata.json`，并用合并结果同步正文中的平台下载表格。
5. 缺失或失败的平台不会清空旧记录；整个过程中 prerelease 始终保持 prerelease。

重复推送或移动同一个 tag 会复用 prerelease 并覆盖同名资产及摘要，只适合 CI 重试或替换尚未安装的构建。已经安装相同 `(app_version, revision)` 的客户端不会再次下载；若要向这些用户发布新构建，必须提高 revision 并创建新 tag，例如从 `pre-release-v7.1.3-1` 改为 `pre-release-v7.1.3-2`。revision 可以跳号，但不能回退。

### 手动上传后 refresh

为节省构建资源，可把 updater 兼容的 Windows x86_64 或其他平台归档手动上传到 prerelease，然后运行 [`.github/workflows/refresh-update-metadata.yml`](../.github/workflows/refresh-update-metadata.yml)。

`prerelease_tag` 留空时，refresh 选择仓库中最新发布的 prerelease；也可以显式指定另一个 prerelease。工作流 checkout 该 tag，用共用版本脚本解析 base、revision 和完整版本，并验证源码的 `AppVersion` 与 `AppVersionStr`，然后按上表的精确文件名扫描四个活跃平台。它只下载和验证命中的归档，生成 fragments 并滚动覆盖唯一普通 Release 的 metadata。名称不匹配的安装器或附件会被忽略。

Windows x86_64 可以手动构建和上传，但二进制必须使用同一个 tag 经过共用版本脚本注入 revision，文件名也必须使用完整 `version_name`。refresh 可以验证名称、摘要、大小和归档布局，但不会从二进制反推其编译 revision。

自动构建和手动 refresh 共用 `update-metadata-release` concurrency group，metadata 写入会串行执行，避免并发读取同一旧版本后互相覆盖。

## 首次迁移与信任边界

旧 updater 不理解 JSON 发行归档，因此 AywGram 7.1.3 及更早版本必须从 prerelease 手动安装一次包含新 updater 的版本。之后才可使用本机制。

本项目把 GitHub 仓库和 Release 的发布权限作为唯一信任边界。仓库账号或 GitHub 发布权限失陷不在本个人项目的威胁模型内；SHA-256 仅用于发现传输或存储损坏，不用于替代可信发布者身份。

## 上游同步政策

同步 AyuGram 或 Telegram Desktop 时，不得恢复旧 HTTP prefix、MTP update channel、Packer、RSA/SHA-1、alpha 私钥、`update_channel`、`update_keys`、`update_verify`、`sign_update.py` 或 canary 签名发布链路。可以按行为移植官方 Updater 的独立平台修复，但不能带回密钥或签名包协议。
