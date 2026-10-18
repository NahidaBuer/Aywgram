---
name: aywgram-release
description: Prepare AywGram distribution releases by reviewing the actual release range, writing bilingual release notes, regenerating the Ayw-only changelog, and validating release readiness before tagging. Use for release preparation and changelog maintenance; compilation and artifact packaging belong to aywgram-build.
---

# AywGram Release

Prepare accurate user-facing release content before the source is tagged. Read [release document format](../../../docs/releases/README.md) and [out layout](../../../docs/agents/out-layout.md). Keep durable task decisions in the relevant task record; put raw release-review evidence in `out/metadata/<target-tag>/` and logs/final conclusions in `out/logs/release/<run-id>/`.

## Establish the release range

Obtain the intended `pre-release-v<base>-<revision>` tag and resolve it through `tools/maintenance/release_version.py` without applying a revision. Do not invent a release version or reuse a published version for changed binaries. A preparation request does not require the target tag to exist yet.

Find the preceding published Ayw distribution prerelease through the repository's Release API, excluding drafts, Telegram `v*` tags and the fixed metadata carrier. Verify its source is an ancestor of the candidate. Honor an explicit baseline instead. If several published releases have incomparable source histories, establish the intended baseline with the user. Record the chosen tag and full commit SHA; do not use an unfetched local tag list as proof of what was published.

Inspect the commit range, relevant diffs, merged histories, patch-equivalent imports and submodule changes. Read the existing upstream-sync evidence when available. Attribute Ayw changes separately from the actual AyuGram and Telegram changes included in this candidate; never copy current upstream release notes for features absent from this source. Inspect the pinned official changelog in `docs/upstream/telegram-changelog.txt`. Browse authoritative upstream/PR sources when needed to verify descriptions and contributors, and retain their links.

## Write and validate

Write `docs/releases/<full-version>.md` using the documented metadata and section contract. Both language summaries should describe user-observable effects, useful distribution fixes, known limitations and relevant upgrade steps. Omit raw commit noise and empty categories. Credit identifiable contributions with evidence; do not credit an entire upstream author list as Ayw contributors. Record unchanged upstream baselines explicitly rather than claiming new upstream features. Meaning and translation require agent review; CI only checks structure and provenance boundaries.

Complete other task/source changes first, then set `reviewed_commit` to the full candidate SHA. A subsequent preparation commit may change only this version's release document and generated root changelog. This avoids a self-referential source hash while making unreviewed later changes fail validation.

Run from the repository root:

```bash
python tools/maintenance/release_notes.py changelog
python tools/maintenance/release_notes.py changelog --check
```

Review the generated root `changelog.txt`: it contains Ayw changes and contributions only, with upstream links. Preserve the separate official log. No historical release backfill is required unless requested.

After the prepared files have been committed through the authorized commit workflow, run:

```bash
python tools/maintenance/release_notes.py validate --tag <target-tag> --source-ref HEAD
```

A successful check permits tagging the reviewed commit when tagging/pushing is authorized. If a substantive change follows `reviewed_commit`, review the added range and update the baseline before regenerating. Never move a published tag to repair release notes.

## Publication boundaries

Preparation does not authorize compilation, pushing tags, editing GitHub Releases, publishing metadata or notifications. Use the build and i18n skills only when their work is needed and authorized. CI consumes the committed release document, preserves the editorial text and updates its marked CI footer; do not ask CI or a model in Actions to infer release summaries.

For an explicitly authorized publication, use the validated source/tag identity and existing workflows. After publication verify the editorial text precedes Ayu/Telegram summaries and the CI block, and that rolling download links describe each platform's actual version. Check publication state before retrying an uncertain external operation.

Report compared commits, included/deferred changes, upstream baselines, contributors and evidence, document paths, validation results and any remaining translation or runtime uncertainty.
