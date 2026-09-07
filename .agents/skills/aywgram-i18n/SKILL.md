---
name: aywgram-i18n
description: Audit and maintain AywGram Desktop English and Simplified Chinese contracts plus the AI-maintained LanguagePacks submodule. Use for localization key drift, missing or stale ayu_ translations, language-source synchronization, release language-pack generation, or localization checks during releases and upstream integrations. Ordinary feature work only requires English and Simplified Chinese.
---

# AywGram i18n

Maintain a small development contract and a separately released set of AI-reviewed language packs. Crowdin and the old `AywGram/Languages` workflow are frozen compatibility sources, not active maintenance targets.

## Canonical sources

- Treat `Telegram/Resources/langs/lang.strings` as the AywGram English key contract and default fallback. AywGram-owned keys use the `ayu_` prefix.
- Treat `Telegram/Resources/langs/zh-hans.lproj/zh-hans.json` as the required Simplified Chinese overlay. It must contain exactly every effective Desktop `ayu_` key.
- Treat `Telegram/Resources/ayw_langpacks` as the `AywGram/LanguagePacks` submodule. It owns the other release translations, generated packs, manifest, and QRC.
- Do not add LanguagePacks sources or generated locale files beneath `Telegram/Resources/langs`.
- Do not modify `../Languages`, its `l10n_main` branch, or Crowdin configuration as part of current maintenance.

## Ordinary feature changes

1. Add or update the English `ayu_` value in `lang.strings`.
2. Add or update the matching key in `zh-hans.json`.
3. Use consistent Simplified Chinese: “您”, “账户”, “聊天”, “消息输入框”, and full-width Chinese punctuation.
4. Run the default audit:

```bash
python3 .agents/skills/aywgram-i18n/scripts/audit_i18n.py --details
```

Do not update all 24 translations and do not advance the submodule for ordinary work. Missing or stale release translations safely fall back to English through per-key source fingerprints.

## Release maintenance

For a release or an explicitly requested translation sweep:

1. Initialize the submodule recursively.
2. Synchronize English and Simplified Chinese into LanguagePacks:

```bash
python3 Telegram/Resources/ayw_langpacks/scripts/sync_sources.py --desktop .
```

3. Use an AI agent to translate only changed or missing keys in `translations/*.json`. Preserve product names, URLs, Markdown structure, and the exact placeholder set. Review meaning in context; never copy English merely to satisfy coverage.
4. If a locale's proposed value fails review, retain its previous valid translation. Omit a new invalid translation so the client uses English.
5. Generate and audit deterministic artifacts:

```bash
python3 Telegram/Resources/ayw_langpacks/scripts/build.py
python3 Telegram/Resources/ayw_langpacks/scripts/audit.py
python3 .agents/skills/aywgram-i18n/scripts/audit_i18n.py --release --details
```

6. Submit and merge the LanguagePacks PR before advancing the Desktop submodule gitlink. The gitlink must reference a commit reachable from the public repository.

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
- Run the default audit during normal development and `--release` before release.
- Run the skill creator's `quick_validate.py` after changing this skill.
- Build Release `Telegram` after C++ or QRC integration changes when the task authorizes a build.
- Report English/Simplified Chinese contract failures separately from allowed translation coverage gaps.
