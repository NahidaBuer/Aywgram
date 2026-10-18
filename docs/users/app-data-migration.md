# 迁移应用数据

AywGram Desktop 将应用名称、可执行文件名和平台包标识与 AyuGram 分离，因此系统会为它选择新的默认数据目录。若首次启动后看不到原有账号，请关闭应用，并将旧目录中的整个 `tdata` 文件夹复制到 AywGram 的数据目录。

## 迁移前

1. 完全退出 AyuGram、Telegram Desktop 和 AywGram，包括托盘或菜单栏中的后台进程。
2. 备份源 `tdata`。不要只复制其中看似与账号对应的子目录；本地密钥和索引同样是登录状态的一部分。
3. 可以先启动一次 AywGram，让它创建目标目录，然后立即退出。
4. 如果目标位置已经有正在使用的 `tdata`，不要直接合并两个目录。请先将目标目录改名备份，再复制完整的源目录。
5. 优先“复制并验证”，确认账号、设置和本地数据正常后，再决定是否删除旧目录。

## Windows

普通安装的常见路径如下：

| 来源 | `tdata` 路径 |
| --- | --- |
| AyuGram Desktop | `%APPDATA%\AyuGram Desktop\tdata` |
| Telegram Desktop | `%APPDATA%\Telegram Desktop\tdata` |
| AywGram Desktop | `%APPDATA%\AywGram Desktop\tdata` |

可以在文件资源管理器地址栏输入 `%APPDATA%`，再进入对应目录。安装程序默认也将 AywGram 安装到 `%APPDATA%\AywGram Desktop`。

解压运行或自定义安装时，程序可能直接使用可执行文件旁的 `tdata`。此时应从旧的 `AyuGram.exe` 所在目录复制到新的 `AywGram.exe` 所在目录。

## macOS

非沙箱版本的常见路径如下：

| 来源 | `tdata` 路径 |
| --- | --- |
| AyuGram Desktop | `~/Library/Application Support/AyuGram Desktop/tdata` |
| Telegram Desktop | `~/Library/Application Support/Telegram Desktop/tdata` |
| AywGram Desktop | `~/Library/Application Support/AywGram Desktop/tdata` |

在 Finder 中选择“前往”→“前往文件夹…”，粘贴上述路径即可。`~/Library` 默认是隐藏目录。

沙箱或商店构建通常位于 `~/Library/Containers/<包标识>/Data/Library/Application Support/`。本项目的新包标识是 `one.aywgram.AywGramDesktop`，旧 AyuGram 构建通常使用 `one.ayugram.AyuGramDesktop`；实际目录可能因打包方式或 Debug 后缀而不同。

## Linux

普通包、归档包和 AppImage 默认通过 Qt 的应用数据目录保存数据：

| 来源 | 默认 `tdata` 路径 |
| --- | --- |
| AyuGram Desktop | `${XDG_DATA_HOME:-$HOME/.local/share}/AyuGramDesktop/tdata` |
| Telegram Desktop | `${XDG_DATA_HOME:-$HOME/.local/share}/TelegramDesktop/tdata` |
| AywGram Desktop | `${XDG_DATA_HOME:-$HOME/.local/share}/AywGramDesktop/tdata` |

若设置了 `XDG_DATA_HOME`，应使用该变量的实际值，而不是 `~/.local/share`。

Flatpak 会将数据放在应用沙箱中，常见路径为：

| 来源 | Flatpak `tdata` 路径 |
| --- | --- |
| AyuGram Desktop | `~/.var/app/com.ayugram.desktop/data/AyuGramDesktop/tdata` |
| Telegram Desktop | `~/.var/app/org.telegram.desktop/data/TelegramDesktop/tdata` |
| AywGram Desktop | `~/.var/app/com.aywgram.desktop/data/AywGramDesktop/tdata` |

Snap 的根目录由包名和修订版本决定，通常可以从 `$SNAP_USER_DATA` 或 `~/snap/<包名>/current/` 开始查找，再进入 `.local/share/<应用名>/tdata`。由第三方维护的 Flatpak、Snap 或发行版包也可能覆盖应用名称或数据目录。

## 便携模式和自定义工作目录

- 若程序旁存在 `TelegramForcePortable`，实际数据目录是 `<程序目录>/TelegramForcePortable/tdata`。该兼容性目录名没有随品牌变化；在同一便携目录中替换可执行文件通常不需要移动数据。
- 使用 `-workdir <目录>` 启动时，实际数据目录是 `<目录>/tdata`，上述平台默认路径不再适用。
- 无法判断时，可在应用的 `log.txt` 中查找 `Working dir:`。`tdata` 就位于该工作目录下。分享日志前请先检查其中是否包含用户名、路径或其他隐私信息。

## 验证与回退

复制完成后启动 AywGram，确认所有预期账号都存在，并检查关键设置和本地消息。若启动异常，请再次完全退出应用，将新复制的 `tdata` 改名保留，再恢复迁移前备份。

不同版本之间的数据格式仍可能变化。本说明只解决默认路径因项目改名而分离的问题，不承诺 AywGram、AyuGram 与 Telegram Desktop 的任意未来版本始终可以互换 `tdata`。

---

# Migrating application data

AywGram Desktop uses application, executable, and package identifiers separate from AyuGram, so the operating system selects a new default data directory. If your accounts are missing after the first launch, quit the application and copy the complete `tdata` directory from the old location to AywGram's data directory.

## Before migrating

1. Fully quit AyuGram, Telegram Desktop, and AywGram, including tray or menu-bar processes.
2. Back up the source `tdata`. Do not copy only the account-looking subdirectories: local keys and indexes are also part of the signed-in state.
3. You may launch AywGram once to create its target directory, then quit it immediately.
4. Do not merge two `tdata` directories that have both been used. Rename and preserve the target directory first, then copy the complete source directory.
5. Copy first and verify the result. Remove the old directory only after accounts, settings, and local data work as expected.

## Windows

| Source | `tdata` path |
| --- | --- |
| AyuGram Desktop | `%APPDATA%\AyuGram Desktop\tdata` |
| Telegram Desktop | `%APPDATA%\Telegram Desktop\tdata` |
| AywGram Desktop | `%APPDATA%\AywGram Desktop\tdata` |

Enter `%APPDATA%` in the File Explorer address bar, then open the corresponding directory. The installer also places AywGram in `%APPDATA%\AywGram Desktop` by default.

An extracted or custom installation may use the `tdata` beside its executable. In that case, copy it from the directory containing the old `AyuGram.exe` to the directory containing the new `AywGram.exe`.

## macOS

| Source | Non-sandboxed `tdata` path |
| --- | --- |
| AyuGram Desktop | `~/Library/Application Support/AyuGram Desktop/tdata` |
| Telegram Desktop | `~/Library/Application Support/Telegram Desktop/tdata` |
| AywGram Desktop | `~/Library/Application Support/AywGram Desktop/tdata` |

In Finder, choose Go → Go to Folder and paste the path. `~/Library` is hidden by default.

Sandboxed or store builds are commonly under `~/Library/Containers/<bundle-id>/Data/Library/Application Support/`. The new bundle identifier is `one.aywgram.AywGramDesktop`; older AyuGram builds commonly use `one.ayugram.AyuGramDesktop`. Packaging and Debug suffixes may change the exact directory.

## Linux

| Source | Default `tdata` path |
| --- | --- |
| AyuGram Desktop | `${XDG_DATA_HOME:-$HOME/.local/share}/AyuGramDesktop/tdata` |
| Telegram Desktop | `${XDG_DATA_HOME:-$HOME/.local/share}/TelegramDesktop/tdata` |
| AywGram Desktop | `${XDG_DATA_HOME:-$HOME/.local/share}/AywGramDesktop/tdata` |

When `XDG_DATA_HOME` is set, use its actual value instead of `~/.local/share`.

Common Flatpak paths are:

| Source | Flatpak `tdata` path |
| --- | --- |
| AyuGram Desktop | `~/.var/app/com.ayugram.desktop/data/AyuGramDesktop/tdata` |
| Telegram Desktop | `~/.var/app/org.telegram.desktop/data/TelegramDesktop/tdata` |
| AywGram Desktop | `~/.var/app/com.aywgram.desktop/data/AywGramDesktop/tdata` |

For Snap, start from `$SNAP_USER_DATA` or `~/snap/<package>/current/`, then look under `.local/share/<application>/tdata`. Third-party Flatpak, Snap, and distribution packages may override the application name or data directory.

## Portable and custom working directories

- If `TelegramForcePortable` exists beside the executable, data is stored in `<application directory>/TelegramForcePortable/tdata`. This compatibility directory name did not change with the brand, so replacing the executable in the same portable directory normally requires no data move.
- With `-workdir <directory>`, data is stored in `<directory>/tdata` instead of the platform default.
- If the location is unclear, look for `Working dir:` in the application's `log.txt`; `tdata` is directly under that directory. Check logs for usernames, paths, and other private information before sharing them.

## Verification and rollback

Launch AywGram after copying and verify every expected account, important setting, and local message. If it fails to start, fully quit it, rename and preserve the copied `tdata`, and restore the backup made before migration.

Data formats may still change between versions. This guide only addresses default directories separated by the project rename; it does not promise that arbitrary future AywGram, AyuGram, and Telegram Desktop versions will always accept each other's `tdata`.
