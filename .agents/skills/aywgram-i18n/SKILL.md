---
name: aywgram-i18n
description: Audit and maintain AywGram Desktop localization catalogs, bundled Simplified Chinese, and the sibling Languages repository. Use for localization key drift, missing or stale ayu_ translations, language-source synchronization, periodic i18n maintenance, or localization checks during releases and upstream integrations. Do not require every translated locale to be updated with each feature change.
---

# AywGram i18n

Keep the compiled Desktop catalog, bundled Simplified Chinese, and the sibling `Languages` source aligned without making every feature change translate every locale.

## Locate the catalogs

- Treat `Telegram/Resources/langs/lang.strings` as the Desktop key contract and English fallback. AywGram-owned keys use the `ayu_` prefix.
- Treat `Telegram/Resources/langs/zh-hans.lproj/zh-hans.json` as the bundled Simplified Chinese overlay. It must mirror every effective Desktop `ayu_` key.
- Treat `../Languages/values/langs/en/Shared.json` as the translatable source for Ayu/AywGram strings. Preserve shared Android entries and platform overrides.
- Treat non-English `Shared.json` files as translations that may intentionally lag and fall back to English.
- Treat `l10n_main` as a generated Crowdin export. Do not edit it directly unless the user explicitly requests generated-branch maintenance.

## Choose the maintenance level

For an ordinary feature change:

1. Add or update the English `ayu_` value in `lang.strings`.
2. Add or update the bundled Simplified Chinese value in `zh-hans.json`.
3. Add the English source value to `Languages/en` when the key should enter the translation workflow.
4. Do not block the change on every other locale. Missing translations use the English fallback.

For a periodic audit, release preparation, or upstream integration that changes localization, run:

```bash
python3 .agents/skills/aywgram-i18n/scripts/audit_i18n.py
```

Use `--details` to list drift. Use `--strict` when bundled Simplified Chinese or the English translation source must be complete. Use `--strict-translations` only for an explicitly requested full translation sweep.

Check or repair the Desktop-specific English source with:

```bash
python3 .agents/skills/aywgram-i18n/scripts/sync_english_source.py
python3 .agents/skills/aywgram-i18n/scripts/sync_english_source.py --write
```

The first command is read-only. Run `--write` only when the task authorizes edits to the sibling `Languages` repository. The synchronizer preserves shared and Android values by adding or updating `_PC` overrides.

Perform the periodic audit before releases, after localization-heavy upstream integrations, and during dedicated i18n maintenance. Do not turn it into a requirement for unrelated changes with no localization impact.

## Repair drift

- Fix missing or stale bundled Simplified Chinese immediately.
- Fix missing or stale `Languages/en` source entries before asking translators to work; otherwise translations cannot cover the Desktop key.
- Preserve `_Android` entries. Prefer an existing `_PC` override when updating a Desktop value.
- Map plural suffixes between Desktop `#one` / `#other` and Shared JSON `_one` / `_other`.
- Preserve placeholder meaning. Shared JSON may use `%1$d`, `%2$d`, `%1$s`, and `%2$s`; Desktop resolves them to `{count}`, `{count1}`, `{item}`, and related placeholders.
- Do not delete source or translation extras merely because Desktop does not recognize them. They may serve Android or older supported clients; classify them before removal.
- Batch non-English translation work periodically. Translate user-visible meaning rather than copying English merely to satisfy the key checker.

## Validate

Run the audit, parse every changed JSON file, and run `git diff --check` in both repositories. Keep files UTF-8 without BOM and preserve the checkout's line endings. Do not build unless the user explicitly authorizes it.

Report blocking catalog drift separately from allowed translation lag, list changed repositories, and state whether Crowdin still needs to regenerate `l10n_main`.
