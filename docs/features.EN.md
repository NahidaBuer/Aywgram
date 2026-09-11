# AywGram Features and Provenance

[简体中文](features.md) · [Chinese-only feature changelog](change-log.md)

This document inventories AywGram features that differ from Telegram Desktop and records both how each implementation entered the project and the known origin of its idea. The audited implementation baseline is `102ecf4494` from September 11, 2026. Standard Telegram Desktop features are not repeated.

## Provenance labels

| Label | Meaning |
| --- | --- |
| AywGram-original | First found on an AywGram-maintained line with no attribution to another client in the commit or source history. This records the implementation lineage found here, not an exclusive claim of invention. |
| External idea | A commit, co-author record, or source comment explicitly names another client; AywGram commonly reimplemented or adapted it to the current Telegram Desktop architecture. |
| Ayu upstream | Inherited through AyuGram history and the `ayu` source subsystem. A few of these ideas have their own third-party credits in Ayu history and are listed separately below. |
| Telegram upstream | Implemented by official Telegram Desktop and called out only where it could otherwise be mistaken for an AywGram-only feature. |
| Mixed | The base came from upstream or an external idea and AywGram added independent extensions. |

Evidence is ranked as follows: first-hand maintainer clarification; explicit commit messages, co-author records, or source comments; introduction commits and file history; and finally current source differences. The document does not guess when the available information cannot establish provenance.

## Feature overview

| Area | Current capabilities | Main provenance |
| --- | --- | --- |
| Ghost Mode | Independently controls message reads, Story reads, online presence, typing/upload state, and automatic offline packets; supports per-account or global settings, locked sub-options, read-on-interaction, ghost-scheduled sending, default silent sending, and a warning before viewing Stories. | Ayu upstream |
| Local history and anti-recall | Stores deleted messages, edit history, and optional attachments locally; views or clears deleted records per chat, keeps local read dates and approximate last-seen observations, and imports or exports the local database. | Ayu upstream, enhanced on the Irena line |
| Message filtering | Regex include/exclude rules, shared and per-chat rules, import/export, quick add, Shadow Ban, blocked-user hiding, and temporary display of filtered results. | Ayu upstream, enhanced on the Irena line |
| Forwarding and repeat | AyuForward, repeat, no-quote and no-caption forwarding, forwarding to Saved Messages, a quick-forward menu, comment-after-forward, and a no-quote action in the multi-select bar. | Mixed: Ayu upstream, Irena line, Yurigram enhancements, and AywGram adaptation |
| Message tools | Message screenshots, send-as-sticker, message details, peer/message IDs, callback-data copying, creation/join/forward/read dates, jump to beginning, expiring-media controls, and a local raw JSON viewer. | Mixed; JSON viewer adapted from Yurigram |
| Search and navigation | Message-type search, an exact local intersection of sender and type, nickname completion, messages-from-member lookup, jumps from a message into Shared Media, and restoration of hidden pinned messages. | Mixed: AywGram-original, Irena line, and Yurigram ideas |
| Appearance and layout | Application and monospace fonts, themes and accents, avatar and bubble radii, wide-message width and alignment, unlimited right-column width, separate in-message and panel sticker scales, message-tail and bottom-info styles, application icons, and folder/drawer/tray customization. | Ayu upstream with AywGram extensions; icon-picker idea from Forkgram |
| Content and media | Optional media metadata, Live Photo detection/playback/export, macOS Force Click media preview and quick reactions, Story downloads, and seeking in voice or round-video messages. | Mixed: Ayu upstream and AywGram-original |
| Editing and automation | Markdown import into the Rich Text Editor, rule-based link-preview rewriting, matched-URL Inline Bot queries, automatic whole-chat translation, and rearrangeable message context menus. | Nagram idea, implemented and extended by AywGram |
| Session transfer and cloud drafts | Imports or exports Pyrogram session strings and Mithka JSON, with per-account controls for plain-text cloud drafts. | Mixed: session transfer was conceived jointly by the AywGram maintainer and Mithka developer, then informed by [Nagram-iOS PR #53](https://github.com/NextAlone/Nagram-iOS/pull/53); cloud-draft controls are an AywGram implementation |
| Cloud settings sync | Synchronizes an allowlist-filtered settings backup through Telegram Mini App CloudStorage. | Primarily inspired by NekoGram, Nagram, and other mobile clients, with a desktop implementation by AywGram |
| UI reduction and conveniences | Hides ads, Stories, similar channels, Premium marks, notification badges, and All Chats; configures the channel bottom button, community-avatar clicks, send confirmations, WebView dimensions/UA, compose controls, drawer items, and tray items. | Mostly Ayu; selected ideas from 64Gram, Kotatogram, and Yurigram plus AywGram extensions |
| Identity and delivery | Streamer Mode, local Premium appearance, project developer/supporter/official-resource badges, bundled language packs, and updates driven by GitHub Release metadata. | Ayu upstream and AywGram-original |

## AywGram-original implementations

The following major features are supported by local commits and source history without attribution to an external client. A later similar implementation elsewhere would not alter the historical provenance recorded here.

| Feature | Summary | Primary evidence |
| --- | --- | --- |
| Wide-screen and chat-layout extensions | Removes the right-column width cap, left-aligns wide-screen messages, scales in-message stickers and the sticker panel separately, and adds a messages-from-this-user entry. | `858fd73163`, `5a6b52f042` |
| Pinned and Shared Media navigation | Restores a hidden pinned message from a menu and jumps from an individual message to its position in Shared Media. | `807d0ccf89`, `a57f3693c5` |
| Optional media metadata | Shows file, codec, and streaming-related metadata in the player and media viewer. | `645a4863b3` |
| Sender-and-type intersection search | Probes server-side search streams independently, continues from the smaller stream, validates the second condition locally, and reports continuation, timeout, and safety-limit states. | `d62097d684`; `api_messages_search_intersection.*` |
| Cloud-draft isolation | Per account, ignores remote plain-text draft changes or prevents local plain-text draft uploads; Rich Message drafts are unaffected. | `b07f77ab34`; `ayu_account_settings.*` |
| Live Photo | Recognizes paired photo/video media and provides still and animated viewing, playback controls, and separate video export. | `e62b73a666` |
| Rich-editor import refinement | On top of the Nagram-inspired Markdown entry point, opens the official Rich Text Editor before sending, preserves the draft until import succeeds, and accepts text over 4,096 characters. | `117b832302` |
| AywGram identity badges | Distinguishes AywGram developers, supporters, and official resources in profiles, chat lists, and title areas while preserving upstream badge categories. | `697597cdbf` |
| Release updater | Discovers, verifies, and installs platform updates using a GitHub Release `update-metadata.json` document. | `082474c937` |
| Bundled language packs | Loads validated localization bundles from the maintained LanguagePacks submodule and maintains English and Simplified Chinese contracts. | `20deac6097`, `0ec1b59537` |
| Faster avatar hold preview | Reduces the hold time needed to preview a chat from its avatar; contributed to this project by ZGX089. | `fd77cefc36` |

## Explicitly borrowed or imported client ideas

| Source | Current feature | Evidence and boundary |
| --- | --- | --- |
| Nagram | Rule-based link-preview rewrites and remote metadata, automatic URL-to-Inline-Bot queries, per-chat automatic translation, Markdown-to-Rich-Message sending, configurable message-menu placement, and a dedicated message-menu settings page. | `1586132391` explicitly says “inspired by Nagram.” The desktop implementation adds local-rule priority, remote-rule validation, pinned bot IDs, and privacy consent. |
| Yurigram | The local “View as JSON” message viewer, the comment-after-forward enhancement in Quick Forward, the per-chat settings override framework, ID mention tools, and the community-avatar click policy. | Viewer asset history leads to Revincx's `013d23941f`; current integration and the chat-setting implementations are primarily in `b07f77ab34`, `5998a6f10c`, and `90a487684b`. The provenance is confirmed by the project maintainer. Streaming metadata work on an unmerged branch is not counted as current functionality. |
| Mithka / Nagram-iOS | Pyrogram session-string and Mithka JSON session import/export. | The AywGram maintainer and Mithka developer conceived the feature together early on. The later implementation referenced Mithka's [PR #53 for Nagram-iOS](https://github.com/NextAlone/Nagram-iOS/pull/53). Desktop validation, data-center migration, and account integration are in `5e2d92d0e4` and `ayu/session_transfer/`, imported into the current tree by `4e36649704`. |
| NekoGram / Nagram and other mobile clients | The product idea of backing up and synchronizing settings through Telegram Mini App CloudStorage. | The mobile-client provenance is confirmed by the project maintainer. AywGram's desktop implementation in `f4dc1394b8` and `ayu/cloud/` adds category allowlists, revision conflicts, full validation, restore journals, rollback, and optional proxy sync. |
| 64Gram | The early custom-language base, message-second display, hiding Stories, hiding All Chats, copying callback data, and simplified colorful quotes/replies. | `d24411fde5`, `486e6e681c`, `5a0a93d8d4`, `2ef9724fa2`, `50c507640a`, and `aa0d733cde` directly thank or compare against 64Gram. These entered AywGram through Ayu history. |
| Forkgram | The application icon picker's interaction idea. | `649e0b0412` explicitly says it was inspired by Forkgram. Individual icon assets come from several projects or AywGram itself. |
| Kotatogram / Kotato | The early peer-ID view, pin-without-sound default, and a co-authored contribution to channel-bottom-button configuration. | `09e24083ff`, `15ee46dab4`, and `257b8552f7`, all in Ayu history. |
| exteraGram | extera icon compatibility choices and the basic `.plugin` metadata information box. | `8e26582bc5` and `c91baa5179`. AywGram can inspect plugin metadata but does not currently run plugins. |
| IrenaGram / Irena maintenance line | Seen/reaction menu placement, no-quote and Saved Messages forwarding, Repeat improvements, media in admin-log edit history, Today/Yesterday separators, blocked-user filtering, clearing local deleted records, mutual-contact marks, screenshot badges, retained source avatars and edit dates, nickname search, colored muted-chat badges, and chat-folder drag reordering jointly derived from the Ayu upstream and Irena lines. | The current README credits IrenaGram and the corresponding local history is authored by Irena/Ireina (`453e240bcf` through `0674d3e01c`). Folder reordering was integrated by `5278b78251`; its dual Ayu/Irena provenance is confirmed by the project maintainer. |

## Main capabilities inherited from Ayu upstream

Commit `4e36649704` combined the local maintenance line, Ayu baseline `8e18cb7110`, and the then-current Telegram Desktop baseline. The following major capabilities remain carried by the Ayu subsystem; items with explicit third-party credits in Ayu history are separated in the preceding table.

- Ghost Mode network controls: message/Story reads, presence, typing and upload state, automatic offline packets, read-on-interaction, ghost scheduling, silent sending, and per-account/global configuration.
- Local message storage: anti-recall, edit history, attachment retention and limits, read dates, approximate last seen, and database import/export.
- Regex message filters and Shadow Ban: shared/per-chat rules, exclusions, import/export, and blocked-user filtering.
- AyuForward and message actions: repeat, no-quote/no-caption forwarding, send as sticker, forwarding progress, and related context-menu actions.
- Message screenshots with theme, background, date, reactions, header decorations, colorful replies, and spoiler options.
- Appearance and entry-point customization: themes, fonts, avatar and bubble radii, icons, notification badges, folders, compose field, drawer, tray, channel bottom button, and WebView size/UA.
- Streamer Mode, local Premium appearance, Google/Yandex translation, and macOS Force Click media preview with haptic feedback.
- Message and account diagnostics: peer IDs, message details, creation/join/read dates, data-center information, sticker-pack owners, and expiring-media controls.
- UI reduction and mis-send protection: hiding ads, Stories, similar channels, Premium status, share buttons, and custom backgrounds, plus sticker/GIF/voice/round-video confirmations.

These can be cross-checked in `Telegram/SourceFiles/ayu/`, `AyuSettings`, the English `ayu_` string contract, and AyuGram history. The current `ayugram/dev` is newer than the Ayu baseline merged here; upstream work not yet synchronized is not a current AywGram feature.

## Telegram upstream capabilities

WEB proxy transport through an embedded WebView with a browser fallback is currently an official Telegram Desktop implementation. AywGram inherits and highlights it in the README, but it is neither an AywGram-original nor an Ayu-original feature. The Rich Text Editor framework likewise comes from Telegram Desktop; the Markdown entry point and import-flow changes described above are the AywGram differences.

## Operational boundaries

- Ghost Mode, anti-recall, and local read/online observations are client-side behavior. They cannot guarantee permanent invisibility from Telegram servers or other clients and may be affected by protocol changes.
- Local Premium only changes local presentation. It does not raise account limits or expose the local appearance to other users.
- Pyrogram session strings and Mithka JSON backups contain complete authorization keys; possession may allow login without an SMS code.
- Cloud settings backups live in Telegram Mini App CloudStorage and are not end-to-end encrypted. Proxy addresses and credentials are included only when proxy sync is explicitly enabled.
- Automatic Inline Bot URL queries are disabled by default. When enabled, a matched URL is sent to the third-party bot selected by the rule after an explicit privacy prompt.
- Names and settings locations can move during Telegram or Ayu synchronization. Current source and settings pages are authoritative for feature availability.
