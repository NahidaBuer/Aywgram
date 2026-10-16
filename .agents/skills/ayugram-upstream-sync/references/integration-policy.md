# Integration Policy

## Remote roles

- `ayugram`: public AyuGram. Review it separately from the supplemental source and Telegram.
- `telegram`: official Telegram Desktop. Prefer sequential signed or annotated release tags, followed by separately reviewed `telegram/dev` commits.
- `origin`: the distribution fork's publishing remote. Treat divergent feature branches as candidates, not automatic upstreams.

Record URLs and fetched tips in every maintenance report. Use `git patch-id`, ancestry, blame, and file-level diffs where needed to recognize equivalent patches.

## Selection order

1. Correctness and security fixes with low product risk.
2. Distribution fixes required by the current product.
3. AyuGram compatibility and maintained fork features.
4. Official Telegram releases in chronological order.
5. Post-release `telegram/dev` fixes that remain useful.

Defer broad behavior changes, packaging/CI policy, branding replacement, agent workflow changes, and unrelated platform changes unless explicitly requested. `origin/feature/session-backup-core` remains deferred until the user requests a clean port.

## Conflict rules

- Inspect all three stages with `git show :1:path`, `:2:path`, and `:3:path` when intent is unclear.
- Keep Ayu/distribution settings and UI controls while adopting official APIs, ownership, lifetime, and layout structures.
- Preserve Ayu JSON/KV preferences. For Telegram `QDataStream` settings, append new fields at the final end and guard reads with `!stream.atEnd()` plus a product default.
- Keep AywGram/NahidaBuer application identity in `core/version.h`, Windows resources, packaging, updater files, icons, and macOS assets while advancing compatible version numbers.
- Keep bundled Simplified Chinese initialization and mirror new `ayu_` language keys into `zh-hans.json`.
- Preserve repository-owned `.agents` workflows. Do not accept other upstream Harness workflows, GitHub workflow, Snap, or release-policy changes as application-source conflict resolutions.
- Preserve newer compatible submodule descendants when an official tag points backward. Merge diverged histories only inside the submodule.

## Submodule authorization

The repository owner grants standing authorization, when requesting upstream integration, to make necessary compatibility edits, merge and commit changes, and push compatibility branches to the distribution's existing submodule forks. This includes publishing the resulting submodule source needed to make the Desktop gitlinks remotely reachable; no separate confirmation is required for each such edit or push. Audit-only and planning requests do not authorize publication.

Verify each destination against the tracked `.gitmodules` configuration at the start of the integration and the submodule's remote configuration. Existing distribution forks include those owned by NahidaBuer or AywGram, such as codegen, lib_ui, lib_tl, lib_icu, and LanguagePacks; ownership alone does not authorize a repository absent from that baseline. Keep changes necessary to the requested synchronization and preserve fork behavior. Use dated `compat/` branches, creating a new branch or fast-forwarding an existing compatibility branch without rewriting published history. Record pushed branches and hashes in the maintenance report.

This authorization does not cover creating new remote forks, changing publishing destinations, pushing to official upstream repositories, force-pushing, deleting remote branches, publishing releases, or pushing the Desktop repository. Those actions require separate authorization. Runtime approval controls still apply; if a push is rejected, report the rejected action and reason, preserve the prepared work, and do not bypass the rejection.

## Submodule contract

1. Compare the base, current, and incoming gitlinks.
2. Fetch the submodule's configured remote and check ancestry.
3. Use the incoming descendant directly when it contains the current hash.
4. Keep the current descendant when it already contains the incoming hash.
5. For divergence, create a dated `compat/tdesktop-<version>-YYYYMMDD` branch, merge both intents, validate it, and push it.
6. Confirm the pushed branch advertises the exact final hash before staging the superproject.
7. Stop without committing the gitlink if authentication or reachability fails.

## Checkpoint contract

Each checkpoint must have no unmerged paths, pass `git diff --check`, contain no conflict markers, preserve the required feature probes, and have coherent version/resource/submodule pointers. Do not squash official version checkpoints together during the maintenance run.

Build only after all integration checkpoints and static checks pass and the user authorizes it. Follow the root [build policy](../../../../AGENTS.md#build-and-validation): default to Release when no configuration is named; Debug requires an explicit request or approved diagnostic need and is never a prerequisite for Release. Static integration requires no build, existing executable, test account or in-app testing.
