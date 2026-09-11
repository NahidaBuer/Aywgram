---
name: aywgram-build
description: Build, diagnose, package, and validate AywGram Desktop locally or in CI. Use for development builds, tagged prerelease artifacts, update-metadata packages, build failures, or cross-platform release coordination; do not use for source-only reviews that require no compilation.
---

# AywGram Build

Keep compilation, packaging and release identity consistent across local Windows builds and GitHub-hosted Linux, macOS and Windows builds.

## Establish the build mode

Build only with explicit user authorization. Default an unnamed configuration to Release; use Debug only when requested or approved for a stated diagnostic need.

Choose one mode before touching the build tree:

- `local`: this is the default for an authorized local build without a supplied release tag. Temporarily inject revision `1` before compilation, so its version name is `<AppVersionStr>-1`, such as `7.2.8-1`.
- `unrevisioned development`: use `AppReleaseRevision = 0` only when the user explicitly requests an upstream-style or unrevisioned build. Its version name is the base `AppVersionStr`, such as `7.2.8`, and it must not be uploaded under a revisioned prerelease name.
- `tagged distribution`: build the exact commit named by `pre-release-v<base>-<revision>`, with revision `1..99` injected before compilation. Its binary, archive, release tag and metadata must all use the same complete version name, such as `7.2.8-1`.

Read only the selected platform guide before inspecting or running its toolchain: [Windows](../../../docs/agents/building-windows.md), [macOS](../../../docs/agents/building-macos.md), or [Linux/WSL](../../../docs/agents/building-linux.md). Honor checkout-local overrides without copying their machine-specific facts into tracked files.

## Preserve the release identity

Treat these values as one atomic release identity:

- the tag and source commit;
- `AppVersion` and `AppVersionStr`, which describe the upstream base version;
- `AppReleaseRevision`, which distinguishes distribution releases of that base;
- the displayed `version_name`, archive name and metadata `(app_version, revision)` pair.

Resolve every build through `.github/scripts/release_version.py`. For a default local build, first resolve the revision-zero source without a tag, form `pre-release-v<base>-1` from the returned base, and pass that value back with `--apply` before compilation. This is a local version injection and does not claim that the corresponding Git tag exists. CI and tagged local builds must instead pass the actual release tag. The script changes only `AppReleaseRevision`; it does not append digits to `AppVersion`.

Never rename an unrevisioned archive to a revisioned name. Metadata validation cannot infer the compiled revision from an executable, so relabeling a revision-zero binary can cause it to repeatedly offer the revisioned update after installation.

Keep `AppReleaseRevision = 0` in committed source. Before any local injection, require `Telegram/SourceFiles/core/version.h` to have no staged or unstaged user change. Build and package while the temporary revision is present, then verify that the revision is the only difference, restore the original file and confirm it is clean. Apply the same guarded cleanup after failure; never use a broad reset or discard a pre-existing change.

Do not publish a default local `-1` artifact merely because its filename looks revisioned. Publication additionally requires a real matching tag at the built source commit and the tagged-distribution checks below. If the user supplies a release tag such as `pre-release-v7.2.8-2`, its revision `2` overrides the local default.

## Build a tagged Windows x86_64 release locally

Use the fast local Windows machine for x86_64 without weakening the shared release contract:

1. Verify the intended tag resolves to the current `HEAD`, recursive submodule gitlinks match that commit, and `Telegram/SourceFiles/core/version.h` passes the common pre-injection cleanliness check.
2. Run `.github/scripts/release_version.py --tag <tag>` without `--apply` and record the resolved `app_version`, `revision`, `version_name` and tag.
3. Run the same command with `--apply` immediately before the authorized Release build. Treat the resulting `version.h` edit as temporary build input, not a commit.
4. Follow the Windows guide to build `Telegram` and package `AywGram.exe`, `Updater.exe` and optional `modules/` while the injected revision is still present. The ZIP must be `out/dist/AywGram-v<version_name>-windows-x86_64.zip`.
5. Validate the ZIP with `.github/scripts/update_release_metadata.py fragment`, using the recorded identity and an output below `out/`. This checks the exact filename, layout, size and digest before upload or metadata refresh.
6. Apply the common guarded cleanup after packaging and validation or after a failed build.

Uploading the ZIP, pushing a tag, editing a GitHub Release, publishing metadata or sending notifications each remains a separate external action requiring user authorization.

## Keep platform artifacts compatible

| Target | Archive | Required layout | Non-obvious constraint |
| --- | --- | --- | --- |
| Windows x86_64 | ZIP | `AywGram.exe`, `Updater.exe`, optional `modules/` at the root | The locally compiled revision must match the prerelease tag before packaging. |
| Windows ARM64 | ZIP | Same Windows layout | Build and dependencies must both target ARM64; do not reuse an x64 CMake tree. |
| macOS ARM64 | ZIP | One `AywGram.app` containing the app executable and Frameworks updater | Strip/thin all Mach-O files, then re-sign and verify; `ditto` preserves the bundle correctly. |
| Linux x86_64 | tar.gz | Executable `AywGram` and `Updater` only | Preserve executable modes; use the Linux/Docker toolchain rather than native Windows tools on a WSL checkout. |

Do not share one `out/` configuration across operating systems or architectures. Verify its generator, source directory, platform and dependency paths before reuse. An old executable or archive is not evidence that the current source built successfully.

Generate metadata fragments only for successful revisioned builds. Asset names must contain the complete version, and each platform advances independently. A missing platform keeps its previous metadata entry; a published replacement for users already on the same `(app_version, revision)` requires a higher revision rather than a same-tag rebuild.

Stop on Windows compiler, PDB or executable file locks and ask the user to close the process or debugger. On macOS, do not report a distributable artifact until architecture and code-signature verification pass. On Linux, distinguish native, WSL and Docker paths and preserve Unix executable bits.

## Report completion

Report the mode, tag and source hash, configuration and architecture, resolved release identity, build command, new artifact path, archive contents, size and SHA-256, metadata-fragment validation, temporary revision cleanup, remaining warnings and any runtime checks not performed. A successful compile proves compilation, not UI correctness or publication approval.
