---
name: aywgram-i18n
description: Maintain AywGram Desktop localization during feature development, key audits, upstream integrations, and releases. Synchronize English and Simplified Chinese with the LanguagePacks submodule, translate changed keys across supported locales, regenerate packs, and coordinate submodule and Desktop commits.
---

# AywGram i18n

Complete localization with the feature that changes it: English and Simplified Chinese first, then the LanguagePacks sources, translations, generated artifacts, and commits in both repositories. Crowdin and the old `AywGram/Languages` workflow are frozen compatibility sources, not active maintenance targets.

## Canonical sources

- Treat `Telegram/Resources/langs/lang.strings` as the AywGram English key contract and default fallback. AywGram-owned keys use the `ayu_` prefix.
- Treat `Telegram/Resources/langs/zh-hans.lproj/zh-hans.json` as the required Simplified Chinese (zh-cn) overlay. Its repository locale code is `zh-hans`; it must contain exactly every effective Desktop `ayu_` key, without the prefix.
- Treat `Telegram/Resources/ayw_langpacks` as the `AywGram/LanguagePacks` submodule. It owns the other release translations, generated packs, manifest, and QRC.
- Do not add LanguagePacks sources or generated locale files beneath `Telegram/Resources/langs`.
- Do not modify `../Languages`, its `l10n_main` branch, or Crowdin configuration as part of current maintenance.

## Ordinary feature changes

1. Implement the feature. If it adds, changes, or removes localization keys, update the English `ayu_` contract in `lang.strings` first.
2. Add, update, or remove the matching keys in `zh-hans.json`.
3. Use consistent Simplified Chinese: “您”, “账户”, “聊天”, “消息输入框”, and full-width Chinese punctuation.
4. Initialize `Telegram/Resources/ayw_langpacks` if needed. Record the Desktop base commit before the feature changes (for a backfill, use the parent of the first affected commit). Inspect the English source diff before synchronization so changed meanings are reviewed in every locale.
5. Synchronize English and Simplified Chinese into LanguagePacks, from the Desktop root:

```bash
python Telegram/Resources/ayw_langpacks/scripts/sync_sources.py --desktop .
```

6. Translate the feature's new and changed keys in every locale listed in `scripts/common.py::LOCALES`; remove retired keys from every translation. Preserve product names, URLs, Markdown structure, and placeholders. Review meaning in context; never copy English merely to satisfy coverage. Unchanged historical gaps can be handled in a separate translation sweep, but do not defer the feature's localization until release.
7. Generate the packs and run the full audit:

```bash
python Telegram/Resources/ayw_langpacks/scripts/build.py
python .agents/skills/aywgram-i18n/scripts/audit_i18n.py --base-ref <desktop-base-commit> --details
```

The default audit checks both Desktop contracts, synchronized submodule sources, translation validity, and generated artifacts. `--base-ref` also requires every new or changed English key since that Desktop commit in every locale, including after the submodule has been committed. Without it, the audit detects changed keys against Desktop `HEAD` and the submodule's committed English source; keep the explicit base for committed features and backfills. `--release` remains a compatible alias for the same full checks.

8. Review diffs and run `git diff --check` in both repositories. When committing the feature, commit LanguagePacks sources, translations, and generated `dist/` first; then commit the Desktop feature, English/Simplified Chinese files, and updated submodule gitlink. Stage only the task's files and preserve unrelated changes.

Local commits complete a commit-only request. Before publishing the Desktop gitlink, publish the LanguagePacks commit through the authorized branch or PR workflow and verify it is reachable from the public remote. Online language updates use LanguagePacks `main`. Do not infer push, PR, merge, or release-upload authorization from a request for local commits.

## Release maintenance and translation sweeps

Use the same workflow for releases and explicitly requested sweeps. Expand translation review to the requested historical gaps and report any remaining coverage gaps by locale. Runtime English fallback is a resilience mechanism, not completion of a feature's translation work. If a proposed value cannot be validated, report the unresolved translation rather than claiming the feature is complete.

`scripts/build.py` assigns fingerprints from the current English source to all translations; it cannot prove that old translations were reviewed after an English change. Review changed meanings before generation. Keep generated `dist/` bytes exactly as the submodule generator writes them (LF), because manifest sizes and hashes depend on those bytes.

AI translation is an agent-driven review step. Never call a model from GitHub Actions; CI only runs deterministic parsing, placeholder, fingerprint, manifest, hash, and generated-tree checks.

## Runtime contract

- `dist/manifest.json` describes schema, revision, relative locale paths, sizes, and SHA-256 hashes.
- Each translated entry records the SHA-256 fingerprint of the exact English source value it translates.
- The client may apply only known, non-empty keys whose English fingerprint and placeholders still match.
- All 24 last-valid packs are embedded through `dist/ayw_langpacks.qrc`; the canonical Simplified Chinese overlay remains in `ayu.qrc`.
- Online updates use `https://cdn.jsdelivr.net/gh/AywGram/LanguagePacks@main/dist/manifest.json`.
- Hash or parse failure keeps the embedded or last valid version. The versioned cache uses atomic writes. Legacy `tdata/ayu/languages/*.json` files remain untouched and unread.

## Validate

- Parse all changed JSON and run `git diff --check` in both repositories.
- Run the default full audit during normal development; supply `--base-ref` for the feature or integration range before and after committing.
- Run the skill creator's `quick_validate.py` after changing this skill.
- Build Release `Telegram` after C++ or QRC integration changes when the task authorizes a build.
- Report contract failures and missing feature translations separately from historical coverage gaps. Static checks do not establish translation fluency or UI correctness.
