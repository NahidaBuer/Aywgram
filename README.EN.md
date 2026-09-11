# AywGram Desktop

[简体中文](README.md)

![Logo 1](Telegram/Resources/art/ayu/nahida/app.png)

AywGram Desktop is an unofficial Telegram desktop client based on
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop) and incorporating AyuGram
changes. This repository provides the complete source for review, local builds, and
contributions. It is not an official Telegram client and is not affiliated with Telegram
or the AyuGram project.

## Project rename and data migration

This project was not rebuilt from scratch, so some internal identifiers intentionally
remain AyuGram. Around the early 7.0 releases, its `tdata` should still be interchangeable
with upstream builds, but no promise is made about future data compatibility.

If the project rename makes existing accounts appear to be missing, manually migrate the
`tdata` directory. See [Migrating application data](docs/app-data-migration.md).

## Features

Built on AyuGram and Telegram Desktop, AywGram focuses on privacy, message control, and deep customization. It offers Ghost Mode, anti-recall and edit history, message filtering and forwarding enhancements, wide-screen layouts, Live Photos, session transfer, and cloud settings sync. It also adapts ideas from clients such as Yurigram, Nagram, and NekoGram while continuing to add original desktop experiences. See [Features and provenance](docs/features.EN.md).

~~AywGram is my personal project.~~

Features may change with upstream synchronization and this repository's maintenance work.
Use this repository's [Releases](https://github.com/NahidaBuer/AywGram/releases) for source-matching builds. Third-party
packages are not maintained here.

## Building and documentation

- [Documentation index](docs/README.md)
- [Windows x64 build](docs/building-win-x64.md)
- [macOS build](docs/building-mac.md)
- [Linux build](docs/building-linux.md)
- [Migrating application data](docs/app-data-migration.md)
- [Session transfer and security](docs/session-backup.md)
- [Contributing](.github/CONTRIBUTING.md)

Builds use the Telegram API identity consistent with AyuGram; see
[API credentials](docs/api_credentials.md). Never commit session files, `tdata`, proxy
secrets, or build artifacts.

## License and credits

This project is licensed under [GNU GPLv3](LICENSE), retaining its OpenSSL linking
exception. It derives from Telegram Desktop and includes code from AyuGram and many
third-party dependencies. Copyright notices and licenses for each component continue to
apply; see [LEGAL](LEGAL) and the license files in dependency directories.

### Telegram clients

- [Telegram Desktop](https://github.com/telegramdesktop/tdesktop)
- [64Gram](https://github.com/TDesktop-x64/tdesktop)
- [Yurigram](https://github.com/Revincx/Yurigram)
- IrenaGram

![Logo 2](Telegram/Resources/art/ayu/nahida2/app.png)
