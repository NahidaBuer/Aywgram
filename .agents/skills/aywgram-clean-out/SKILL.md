---
name: aywgram-clean-out
description: Inventory and selectively clean the documented AywGram out directory by temporary files, build intermediates, retained binaries, distribution packages, compiler caches, downloads, or raw evidence while preserving logs and final reports. Use for disk cleanup and output retention; legacy layouts are handled manually.
---

# AywGram Clean Out

Read the [out layout](../../../docs/agents/out-layout.md) before classifying output. Use the repository helper `tools/maintenance/clean_out.py`; do not substitute a blanket `rm -rf out`, CMake force configure, or `git clean`.

## Choose categories

- `temporary`: extraction, test and packaging scratch.
- `intermediates`: configured build trees and supplemental dependency build output. Native linked replicas inside those trees are included; retained programs in artifacts and packages in dist are separate.
- `binaries`: complete retained unpacked programs and app bundles in artifacts.
- `packages`: final distribution archives in dist.
- `compiler-cache`: reproducible compiler cache data.
- `downloads`: reproducible downloaded cache contents.
- `evidence`: raw audits, metadata previews/fragments, temporary plans and patches. Preserve unique uncommitted fixes or recovery material before selecting this category.

Logs and final reports are always retained, including native logs embedded in a selected build tree. Source checkouts, dependency sources, installed tools, configuration/credentials, external Libraries and unknown directories are outside the selectable categories. Do not add old-layout heuristics or automatically migrate old output. If asked to handle legacy files, inspect exact paths and handle them manually under the user's selected scope.

If the user has named categories, that is authorization for those categories: preview, inspect and apply without asking for the same permission again. When the scope is unspecified, show sizes, regeneration costs and the distinction between intermediates/binaries/packages, then ask which categories to remove. No category is deleted by default.

## Preview and apply

Create a unique run directory below `out/logs/cleanup/`. For example:

```bash
python tools/maintenance/clean_out.py scan --category temporary --category intermediates --output out/logs/cleanup/<run-id>/plan.json
```

Inspect the exact candidate paths and preserved-log counts. Confirm there is no active build, package, compiler-cache writer or app using selected output. The command runner's activity markers catch managed build commands; inspect other processes when relevant. Do not kill applications or bypass locks. A candidate containing a Git checkout is refused, even if it was placed in a disposable directory.

Apply only an in-scope plan:

```bash
python tools/maintenance/clean_out.py apply --plan out/logs/cleanup/<run-id>/plan.json
```

The helper rechecks the inventory snapshot, refuses changed candidates and linked deletion roots, verifies embedded log copies before deletion, and writes a cleanup report even on partial failure. It never traverses symlink targets. If the snapshot changed, inspect a fresh preview rather than forcing it. Stop on deletion/locking errors and report the partial result; do not broaden scope or run alternative destructive commands to work around a refusal.

Report removed categories/paths, bytes represented by removed files, preserved logs, untouched packages/programs/caches as relevant, and the report path. Filesystem free-space changes may differ because of sparse files, hard links and snapshots.
