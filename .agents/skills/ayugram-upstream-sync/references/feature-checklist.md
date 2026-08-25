# Feature Preservation Checklist

Use current symbols and call sites rather than requiring old patches byte-for-byte. Search each area after structural conflicts and before the final report.

| Area | Required behavior and useful probes |
| --- | --- |
| Forwarding | Preserve AyuForward, Quick Forward, NoQuote, NoCaption, comments, topics, Saved Messages, ephemeral messages, toasts, and `sendForwardFirst`. Search `AyuForward`, `NoQuote`, `NoCaption`, `sendForwardFirst`. |
| Send lifetime | Preserve `Api::SendCompletion` exactly-once success/failure behavior across upload, send, ShareBox, and AyuForward paths. Search `SendCompletionPtr` and `MakeSendCompletion`. |
| Ghost mode | Preserve read/reaction/upload privacy and ghost scheduling at every send/forward entry. Search `applyGhostScheduling` and `AyuSettings::ghost`. |
| Message display | Preserve message ID settings and bottom-info repaint subscriptions. Search `showMessageId`. |
| Bubble radius | Preserve keyed/runtime radius caching, RAII override, legacy small-radius migration, and the Ayu slider. Search `BubbleRadiusOverride`, `TakeLegacySmallBubbleRadius`, `messageBubbleRadius`. Do not re-expose a migrated obsolete toggle. |
| Stickers | Preserve both in-message sticker scale and sticker-panel scale, including cache invalidation and reactive resizing. Search `messageStickerScale` and `stickerPanelScale`. |
| Filtered replies | Preserve filtered-reply unavailable/ghost behavior without reviving hidden content. Search `FiltersController::filtered` around reply resolution and rendering. |
| Pinned messages | Preserve per-peer hidden pinned state and reset behavior when the top pin changes. Search `hiddenPinnedMessageId` and `HidePinnedBar`. |
| Folders and communities | Preserve safe folder reorder, hidden All Chats handling, community row behavior, pinned order, and folder search. Inspect `window_filters_menu.cpp`, `data_community.cpp`, and folder reorder call sites. |
| Search and shared media | Preserve member message search, user message search, shared-media links, and links-tab routing. Inspect peer menus, profile media adapters, and `Storage::SharedMediaType::Link`. |
| AI compose | Preserve Ayu `showAiEditorButtonInMessageField` while adopting official availability rules and compose APIs. |
| Plugins | Preserve `.plugin` metadata opening before generic image/markdown/file launch handling in `data_document_resolver.cpp`. |
| Localization | Preserve first-run `zh-hans` selection in `AyuInfra::initLang`, bundled Ayu Chinese strings, and every mirrored `ayu_` key. |
| Branding | Preserve AywGram/NahidaBuer names, GUID, executable name, icons, and updater policy while updating version numbers. |
| Chat settings | Preserve the intentionally grouped and ordered Ayu chat submenu; do not regress it to source-registration order. |

For forwarding, inspect ordinary single/group forwarding, selected messages, NoQuote, NoCaption, captions, comments, topics, Saved Messages, ephemeral paths, AyuForward, failure callbacks, and ghost scheduling separately.

For serialization, compare the complete write and read order rather than the conflict hunk alone. Reject any new middle insertion in a sequential stream.

Do not infer UI correctness solely from compilation. Report interface regression testing as pending unless the user explicitly authorizes and supplies the necessary runtime/account context.
