# CI caches

[简体中文](ci-cache.md)

The Windows, macOS and Linux workflows share the `ayw-ci-v3` cache namespace. Tag pushes still publish prereleases. Application compiler caching defaults to enabled on all supported targets, including the repaired macOS ARM64 and Windows ARM64 paths. These repairs still require cold and warm CI validation before merging. Dependency caches remain enabled on every platform.

## Tag releases and shared caches

Keep creating and pushing `pre-release-v<base>-<revision>` tags locally. The tag run dispatches the same Prerelease workflow on the default branch (currently `main`); the actual build checks out the tag's fixed commit SHA. The workflow ref determines cache scope, so release builds and manual prewarming on main can restore and update compatible caches together. Prewarming is optional for routine releases.

Automatic dispatch supplies both `release_tag` and the full `source_sha`, supporting lightweight and annotated tags. Preparation checks that the tag still resolves to that commit, then validates the source version before creating or updating a Release. Platform and metadata jobs all check out the same SHA, independently of later main updates. Do not move or delete a tag while its release is running.

Actions shows two runs: the tag dispatcher and the actual build on main. The dispatcher summary links to the actual run. Successful dispatch only means GitHub accepted the request; follow that link for the build and publication result. Failed or uncertain dispatch requests are not automatically retried: check Actions for an existing build before retrying. Only the dispatcher receives `actions: write` and it uses `GITHUB_TOKEN`, requiring no new PAT. Publication credentials remain unchanged.

To rerun the full release manually, select `main` in **Actions → Prerelease → Run workflow** and enter the existing `release_tag`. Leave optional `source_sha` empty to resolve it automatically, or supply the full matching commit SHA. The full release entry rejects non-default branches. Direct platform dispatches remain available; select main and supply `release_tag` if they should update the shared cache scope. Dispatcher and builder concurrency groups are separate; duplicate builds for the same tag cancel the previous run, while different tags do not cancel one another.

Merge the changes into main before creating a new tag from a commit containing the new workflow and helper scripts. Historical tags do not automatically acquire the dispatcher. The first run may be cold; later hits depend on compatible cache availability, toolchain and dependency changes, and eviction. Default targets, filenames, releases, update metadata and Telegram notifications retain their existing behavior. Windows x64 remains available through the existing manual process.

## Manual prewarming

Run **Actions → Warm CI caches → Run workflow** from `main`. The workflow builds the exact main commit selected by that dispatch, including the application, and saves caches without packaging, uploading artifacts, editing releases or notifying Telegram. It has no schedule or push trigger.

The default `all` target warms Linux x86_64, macOS ARM64 and Windows ARM64. Each target can also be selected individually; Windows x86_64 is available separately and remains excluded from automatic prerelease builds. Runs warming the same platform and architecture use the same concurrency group with `cancel-in-progress: false`. GitHub may replace an older pending run when several requests queue, but an active prewarm is not cancelled by a newer request.

The reusable platform workflows accept `warm_cache=false`, a platform-specific `compiler_cache` default, and optional `source_sha` through `workflow_call`. Compiler caching defaults to true on macOS, Linux and both Windows architectures; setting `compiler_cache=false` bypasses application compiler caching. Warming requires the default branch, empty `release_tag` and `source_sha`, and both `distribute` and `update_metadata` disabled. Direct platform dispatches support warming and the compiler-cache switch where available.

Manual prewarming is useful before the first release, after toolchain or dependency updates, or after a long idle period. Routine releases also update caches on main. Tag pushes do not wait for an active prewarm. Existing tag-scoped caches are neither migrated to main nor proactively deleted. Caches unused for more than seven days can expire, and older snapshots compete for repository storage. See [GitHub's cache scope and eviction rules](https://docs.github.com/en/actions/reference/workflows-and-actions/dependency-caching).

## Cache layers

| Platform | Dependency cache | Application cache |
| --- | --- | --- |
| Windows ARM64 | Separate ThirdParty, non-Qt Libraries, and Qt archives with their prepare-stage markers | sccache 0.17.0, local disk, 1 GiB; CI validation pending |
| Windows x64 | Separate ThirdParty, non-Qt Libraries, and Qt archives with their prepare-stage markers | sccache 0.17.0, local disk, 1 GiB |
| macOS ARM64 | Libraries and ThirdParty in one archive | ccache 4.13.6 through CMake and Xcode compiler wrappers, 1 GiB; CI validation pending |
| Linux x86_64 | Separate Docker BuildKit layers and cache mounts | ccache, local disk, 1 GiB; the container receives an explicit capacity override |

Keys contain the platform, target architecture and toolchain fingerprint. Windows records the active MSVC, Windows SDK and CMake versions; macOS records Xcode, SDK, AppleClang and CMake; Linux records the generated Docker context fingerprint. Dependency hashes cover tracked prepare scripts and `qt_version.py`, or tracked Docker context files after Dockerfile generation. Temporary files, Python bytecode and virtual environments are excluded.

Windows/macOS dependency restore prefixes stay inside the same toolchain boundary. The prepare script still validates each dependency stage after restoration; an Actions cache hit alone never skips this check. x64 uses `Libraries/win64`, while Windows ARM64 uses `Libraries`. On supported targets, compiler cache prefixes also contain the dependency fingerprint, engine version and Release configuration; run ID and attempt make each saved snapshot immutable while allowing a later run to restore the preceding snapshot. Ordinary application source edits do not change the restore prefix.

Windows CI uses Ninja Multi-Config with the selected Native Tools environment and the existing Release settings. Local Visual Studio builds are unaffected. Both Windows architectures can use sccache. Install it only after dependency preparation and cache saves so Meson cannot automatically start the application cache server during dependency builds. Reset statistics with `sccache --zero-stats`, which connects to an existing server or starts one when needed. MSVC PCH remains enabled: sccache bypasses PCH compilations, so its benefit applies to the remaining compilations. Read the uncacheable counters before interpreting the overall hit rate.

macOS prepares dependencies and builds the application on one runner with one Xcode selection. Xcode remains the generator to preserve asset catalogs and bundle processing. Install ccache after dependency preparation. With `compiler_cache=true`, both CMake compiler settings and Xcode CC/CXX settings use the same wrappers, including the compiler call that generates AutoMoc predefines. The wrappers remove non-macOS deployment-target environment variables while preserving `MACOSX_DEPLOYMENT_TARGET`, the SDK and compiler arguments. Linker settings retain the real clang/clang++ paths. Neither the whole Xcode build tree nor final application bundles are cached.

Linux injects cached mounts before building the dependency image and explicitly exports them before saving the mount archive. Export is a normal CLI step using a fixed cache-dance commit, not a deferred action post step. The exported dependency ccache is trimmed to 1 GiB before upload; package-manager mount data is retained. An export failure skips only the mount save and is visible in the step result; the completed Docker layers can still be saved and used to build the application. Dependency caches are saved before application compilation, so a later application failure does not discard successfully prepared dependencies. Application snapshots are saved for small updates as well as large ones; cancelled runs do not save application caches.

Compiler tool downloads use fixed official releases and checked-in SHA-256 digests. Updating a tool requires updating its asset, digest and engine key together. Old remote caches are not deleted by these workflows, and repository storage/billing settings are not changed.

## Reading results and validating changes

Each build appends a cache summary containing the workflow ref (cache scope), release tag, actual source SHA, toolchain, requested/restored keys, exact or fallback matches, dependency and application durations, compiler counters (including uncacheable reasons), local cache sizes and repository cache usage when the API is accessible. A statistics failure is reported as unavailable and prevents saving that compiler snapshot; it is not converted into zero misses. Cache telemetry does not turn a successful application build into a failure.

After implementation, validate cold and warm Release builds on fresh runners for each automatic platform and Windows x64 separately. On supported compiler-cache targets, confirm actual compiler invocation through the wrapper and compiler hits. On all targets, confirm dependency-stage skips, target architecture, Updater, macOS icons and bundle contents, and the existing archive layout. Once publication is authorized, use two different tags to verify both actual builds run on main with their respective source SHAs, and the second restores compatible caches saved by the first. Do not infer successful compilation from a populated cache or a green restore step.

Static checks are `actionlint` with ShellCheck for the platform/prewarm workflows and `python -B -m unittest discover -s .github/scripts -p 'test_*.py'`. Run CI builds and publication only when separately authorized. Bump the namespace to invalidate incompatible cache layouts; do not delete shared caches as a default troubleshooting step.
