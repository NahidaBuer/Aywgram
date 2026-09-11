# Agent Guide for AywGram Desktop

This file defines repository-wide rules for the AywGram -> AyuGram -> Telegram Desktop fork. Read `AGENTS.override.md` when present for checkout-local differences; it is optional and must remain untracked. A fresh checkout must work from this guide without any machine-specific override.

## Product Baseline and Upstream Maintenance

Preserve distribution-specific patches and the existing AyuGram feature set. Accept upstream changes only when useful and compatible with that baseline; upstream parity alone never justifies removing or weakening fork behavior.

Use the repository-owned [upstream sync skill](.agents/skills/ayugram-upstream-sync/SKILL.md) for audits, integration planning and execution. It owns remote verification, separate AyuGram/Telegram review, patch selection, staged integration, conflict resolution, submodule reachability and maintenance reporting. Preserve the current `.agents/` workflows and local maintenance instructions; do not import unrelated skills, Harness workflows or CI replacements as part of application-source integration. Preserve existing user changes before any worktree cleanup.

## Build and Validation

- Use the repository-owned [build skill](.agents/skills/aywgram-build/SKILL.md) for local or CI builds, release packaging, build diagnosis, and update-metadata artifacts. It owns local revision injection, tagged release identity, artifact consistency, and platform-guide routing. A local build without a supplied release tag temporarily uses revision `1` by default; an explicit tag always supplies its own revision, while the committed source remains at revision `0`.
- Build only when the user explicitly authorizes it. An authorized build defaults to Release when no configuration is named. Debug requires an explicit request or approval for a stated diagnostic need; never make Debug a prerequisite for Release.
- Static-only work, including upstream integration, requires no existing executable, test account, build or in-app testing. Run focused static checks for the changed areas and state what remains unverified.
- Before configuring, building, packaging or investigating a toolchain failure, read only the target platform guide: [Windows](docs/agents/building-windows.md), [macOS](docs/agents/building-macos.md), or [Linux/WSL](docs/agents/building-linux.md). Verify the actual configured toolchain before reusing `out/`; machine snapshots are not permanent requirements.
- A successful build proves compilation, not UI correctness or release approval.

## Output and Local Tools

Keep build artifacts, maintenance reports, logs and generated patches under repository-root `out/`. Keep reusable checkout-local scripts under `.local-tools/`, never under upstream-owned directories such as `Telegram/tests/`. Local Python probe output is the explicit exception to the output rule: use `.local-tools/results/`. All local tools, credentials and probe results remain local-only; do not stage or commit anything under `.local-tools/`, or add exceptions for it to tracked `.gitignore`.

## Coding and Reference Routing

Before editing or reviewing code, read the applicable rules in [REVIEW.md](REVIEW.md). It owns mechanical style, source comments, type deduction, QString literals, return types and platform checks. Preserve its constraints; use clear names instead of redundant comments, prefer `auto` and `u"..."_q`, and distinguish Windows/macOS/all-other platforms as the project requires.

Read only the relevant sections of the [coding reference](docs/agents/coding.md):

- [API Usage](docs/agents/coding.md#api-usage) for MTProto calls and response/error handling. Use the checked-out TL schemas as the signature source and honor suppressed UI errors.
- [UI Styling](docs/agents/coding.md#ui-styling) for layout, painting and style changes. Define dimensional values in `.style` and access them through `st::`; never hardcode sizes or coordinates in C++. Animation durations belong in `.cpp` constants, not style files.
- [Localization](docs/agents/coding.md#localization) for immediate/reactive strings, placeholders, counts and rich text projectors.
- [RPL](docs/agents/coding.md#rpl) for producers and subscriptions; preserve explicit producer moves and subscription lifetimes.

## Local Storage Serialization

`Core::Settings` and `Main::SessionSettings` use sequential `QDataStream` serialization. Append new fields only at the final end of the stream, never in the middle. Guard reads with `!stream.atEnd()` and meaningful defaults so older saved data remains readable. Prefer generic KV preferences (`writePref<Type>` / `readPref<Type>`) for simple flags and values rather than extending the binary stream.

## Localization

Feature changes first maintain the English `ayu_` contract in `Telegram/Resources/langs/lang.strings` and matching Simplified Chinese in `Telegram/Resources/langs/zh-hans.lproj/zh-hans.json`, then synchronize `Telegram/Resources/ayw_langpacks`, translate the changed keys across its supported locales, and regenerate its artifacts. Read the [i18n skill](.agents/skills/aywgram-i18n/SKILL.md) when changing these keys or performing localization audits, source synchronization, release preparation or localization-heavy integrations. It owns audit commands and the development workflow, including committing the LanguagePacks changes before the Desktop gitlink. Do not defer a feature's language-pack updates until release; unrelated historical gaps can be handled during dedicated maintenance.

## Text Files

- Use CRLF for project text files on native Windows; follow the Linux guide for WSL/Linux checkouts.
- Use UTF-8 without BOM for source, headers, build/config, style and localization files. Normalization must preserve other content.
- Keep each Markdown paragraph or list item on one line unless syntax requires a deliberate break; never hard-wrap prose to 80 columns.

## Commits and Pull Requests

Use Conventional Commits (`<type>[optional scope]: <description>`). Match recent history with a plain-language subject around 50-60 characters; usually no body is needed. Add at most a line or two explaining what changed when the subject is insufficient. Never add `Co-Authored-By:`, assistant/tool attribution trailers, `Autotask:`, attempt labels or other workflow markers. If asked to create a PR, clearly state in its description that it was AI generated.
