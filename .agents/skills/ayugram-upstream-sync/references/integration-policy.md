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
- Do not accept upstream `.agents`, `.claude`, GitHub workflow, Snap, or release-policy changes as application-source conflict resolutions.
- Preserve newer compatible submodule descendants when an official tag points backward. Merge diverged histories only inside the submodule.

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

Build only after all integration checkpoints and static checks pass. Build Debug first; build Release only when explicitly requested or when the invoking request names the full Debug-then-Release workflow.
