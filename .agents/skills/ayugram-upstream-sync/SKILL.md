---
name: ayugram-upstream-sync
description: Audit, plan, integrate, and build recurring upstream updates for this AywGram Desktop fork while preserving distribution-specific and Ayu behavior, resolving Telegram conflicts, keeping submodule gitlinks remotely reachable, and writing generated artifacts under out/. Use when asked to check upstream, origin, ayugram, or telegram changes; summarize supplemental commits; prepare or perform periodic upstream synchronization; resolve integration conflicts; validate preserved fork features; or build an explicitly authorized configuration after an integration.
---

# AyuGram Upstream Sync

Use a staged, report-first workflow for recurring maintenance of this AywGram fork. Default to a read-only audit when the request does not explicitly authorize integration or builds.

## Establish scope

1. Read repository-root `AGENTS.md` and `AGENTS.local.md` when present.
2. Treat `upstream` as an optional supplemental source, `ayugram` as public AyuGram, `telegram` as official Telegram Desktop, and `origin` as this distribution fork's publishing remote. Verify URLs every run.
3. Select the requested mode:
   - `audit`: fetch, compare, and report only.
   - `plan`: audit and propose ordered integration checkpoints.
   - `integrate`: perform the approved merges or ports and static validation.
   - `build`: run only the explicitly requested configurations after integration.
4. Keep reports, logs, temporary patches, and generated files under `out/upstream-sync/`. Never create root `build/` or `cmake-build-*` directories.

## Audit

Run the bundled audit from the repository root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .agents/skills/ayugram-upstream-sync/scripts/audit-upstreams.ps1 -Fetch
```

Omit `-Fetch` only for an offline snapshot. Read the generated `audit.md`, then inspect relevant commits and diffs directly; do not select patches from subjects alone.

For planning or integration, read [integration-policy.md](references/integration-policy.md). Group incoming work by subsystem, user-visible effect, dependencies, overlap with current history, and conflict risk. Distinguish unique changes from merged or patch-equivalent work.

## Integrate

1. Preserve pre-existing user changes before cleaning the worktree. Record every stash or recoverable move and do not silently drop or commit it.
2. Work on the user-selected branch. Otherwise create `integrate/upstream-tdesktop-YYYYMMDD` from the agreed baseline. Never rename or delete `dev` without explicit authorization.
3. Integrate approved supplemental patches first, selected AyuGram changes second, and Telegram changes last unless topology requires another order. Explain deviations.
4. Merge official Telegram release tags sequentially, one checkpoint commit per version. Review the commits after the newest tag on `telegram/dev` separately.
5. Do not bulk-merge a supplemental branch, `origin/dev`, or an upstream workflow branch merely for parity. Port or merge only reviewed changes authorized by the request.
6. Ignore workflow skills, agent pipelines, and unrelated CI introduced by upstream. Preserve this repository's local `.agents`, `.claude`, release policy, branding, and updater behavior.
7. Resolve conflicts by inspecting the base, ours, and theirs. Preserve distribution-specific and Ayu behavior while adapting it to the final Telegram API and structure. Never resolve a product file by blindly taking one side.
8. Treat submodules as independent integrations. Create and push compatibility commits when histories diverge, then verify the final hash is reachable from the `.gitmodules` URL before staging the superproject gitlink.
9. Commit each checkpoint with a concise repository-style subject. Do not push the main branch unless requested.

## Validate

Read [feature-checklist.md](references/feature-checklist.md) before resolving high-risk conflicts and again before completion.

After every checkpoint:

```powershell
git diff --check
git diff --name-only --diff-filter=U
rg -n "^(<<<<<<<|=======|>>>>>>>)" --glob "!out/**" .
git submodule status --recursive
```

Also verify selected release tags and remote tips are ancestors of `HEAD`, inspect settings serialization and localization changes, and confirm the worktree contains no generated root directories. Treat a clean build as compilation evidence, not UI release approval.

## Build

Build only when the user explicitly authorizes it. Use the configured repository-root `out/` tree. When the user authorizes a build without naming a configuration, build Release. On this native Windows checkout, set UTF-8 compiler input:

```powershell
$env:CL = '/utf-8'
cmake --build out --config Release --target Telegram
```

Build Debug only when the user explicitly requests it. If Debug becomes necessary for diagnosis, explain the need and obtain approval before running it. Never build Debug merely as a prerequisite for Release.

Repair ordinary configure, compile, and link failures within the integration scope, committing focused compatibility fixes. If C1041, LNK1104, an executable/PDB access error, or another file-in-use error occurs, stop immediately and ask the user to close AywGram/Telegram and its debugger. Do not retry or delete locked files.

## Report

Deliver the compared refs and hashes, grouped incoming changes, selected and deferred work with reasons, checkpoint commits, conflict resolutions, submodule branches and reachability, feature-preservation results, build commands and artifacts, remaining warnings, saved user changes, and the recommended next baseline.
