# Agent Build Guide: Windows

Read this file only for native Windows toolchain work. Follow [AGENTS.md](../../AGENTS.md#build-and-validation) for build authorization and configuration selection. Human setup instructions are in the [Windows x64 build guide](../building-win-x64.md).

A checkout opened through `\\wsl.localhost\...` is a Linux checkout. Do not use native Windows CMake on its `out/` tree; follow the [Linux agent guide](building-linux.md).

## Existing Configuration First

Before configuring or building, inspect repository-root `out/CMakeCache.txt`: `CMAKE_COMMAND`, `CMAKE_GENERATOR`, `CMAKE_GENERATOR_INSTANCE`, `CMAKE_GENERATOR_PLATFORM` and `CMAKE_HOME_DIRECTORY`. Verify the referenced executable and installation exist, the source path identifies this checkout, and the architecture and dependency layout match the intended target. Compare any local override snapshot with these facts; no fixed Visual Studio version applies to every machine.

For an existing Visual Studio tree, an ordinary PowerShell session can invoke the cached absolute CMake executable. Missing `cmake` on PATH does not establish a compiler or source failure. Keep CMake and its modules from the same installation; inspect stale module paths before diagnosing regeneration failures.

After the user authorizes a build and the configuration checks pass, use this Release example from the repository root. Use Debug only when explicitly requested or approved for diagnosis:

```powershell
$ayuBuildTree = Join-Path (Get-Location).Path 'out'
$ayuCachePath = Join-Path $ayuBuildTree 'CMakeCache.txt'
if (-not (Test-Path -LiteralPath $ayuCachePath -PathType Leaf)) {
    throw 'No configured out tree. Inspect the environment before configuring.'
}
$ayuCmakeEntry = @(Select-String -LiteralPath $ayuCachePath -Pattern '^CMAKE_COMMAND:[^=]+=(.+)$')
if ($ayuCmakeEntry.Count -ne 1) {
    throw 'Expected one CMAKE_COMMAND in the existing cache.'
}
$ayuCmakePath = $ayuCmakeEntry[0].Matches[0].Groups[1].Value
if (-not (Test-Path -LiteralPath $ayuCmakePath -PathType Leaf)) {
    throw 'The cached CMake executable is missing. Inspect the installation.'
}
$env:CL = '/utf-8'
& $ayuCmakePath --build $ayuBuildTree --config Release --target Telegram
if ($LASTEXITCODE -ne 0) { throw 'Release build failed; do not package existing artifacts.' }
```

If the shell cannot change directories, set `$ayuBuildTree` to the verified absolute `out/` path and use that same tree for cache inspection and the build. The target is `Telegram`; this fork's Release executable is `out/Release/AywGram.exe`.

## Release Packaging

Every successful authorized local Windows x64 Release build includes packaging before reporting completion. Match the distribution layout in `.github/workflows/win.yml`, with generated output under `out/dist/`. Run the following from the repository root only after the current Release build succeeds and the cache confirms x64. The `Telegram` target depends on `Updater` when auto-update is enabled; if either executable is missing, stop and investigate the configuration instead of producing an incomplete ZIP.

Use the version constants from the source used for that build: `AppVersionStr` alone when `AppReleaseRevision` is zero, otherwise `<AppVersionStr>-<AppReleaseRevision>`. This matches CI's `.github/scripts/release_version.py` naming. For a tagged release, ensure the source revision matches the intended `pre-release-v<base>-<revision>` tag before building; changing only the ZIP name does not change the embedded version.

```powershell
$ayuVersionSource = Get-Content -LiteralPath 'Telegram/SourceFiles/core/version.h' -Raw -Encoding UTF8
$ayuBaseMatch = [regex]::Match($ayuVersionSource, 'constexpr auto AppVersionStr = "([0-9]+\.[0-9]+\.[0-9]+)";')
$ayuRevisionMatch = [regex]::Match($ayuVersionSource, 'constexpr auto AppReleaseRevision = ([0-9]+);')
if (-not $ayuBaseMatch.Success -or -not $ayuRevisionMatch.Success) {
    throw 'Missing release version constants.'
}
$ayuVersionName = $ayuBaseMatch.Groups[1].Value
$ayuRevision = [int]$ayuRevisionMatch.Groups[1].Value
if ($ayuRevision -gt 99) { throw 'Release revision must be between 0 and 99.' }
if ($ayuRevision -gt 0) { $ayuVersionName += "-$ayuRevision" }
$ayuReleaseDir = Join-Path (Get-Location).Path 'out/Release'
$ayuItems = @(
    (Join-Path $ayuReleaseDir 'AywGram.exe'),
    (Join-Path $ayuReleaseDir 'Updater.exe')
)
foreach ($ayuItem in $ayuItems) {
    if (-not (Test-Path -LiteralPath $ayuItem -PathType Leaf)) {
        throw "Missing Release executable: $ayuItem"
    }
}
$ayuModules = Join-Path $ayuReleaseDir 'modules'
if (Test-Path -LiteralPath $ayuModules -PathType Container) { $ayuItems += $ayuModules }
$ayuDist = Join-Path (Get-Location).Path 'out/dist'
New-Item -ItemType Directory -Path $ayuDist -Force -ErrorAction Stop | Out-Null
$ayuZip = Join-Path $ayuDist "AywGram-v$ayuVersionName-windows-x86_64.zip"
Compress-Archive -LiteralPath $ayuItems -DestinationPath $ayuZip -CompressionLevel Optimal -Force -ErrorAction Stop
Add-Type -AssemblyName System.IO.Compression.FileSystem
$ayuArchive = [System.IO.Compression.ZipFile]::OpenRead($ayuZip)
try {
    $ayuEntries = @($ayuArchive.Entries.FullName)
    foreach ($ayuRequired in @('AywGram.exe', 'Updater.exe')) {
        if ($ayuRequired -cnotin $ayuEntries) { throw "ZIP is missing $ayuRequired at its root." }
    }
    $ayuEntries
} finally {
    $ayuArchive.Dispose()
}
Get-Item -LiteralPath $ayuZip | Select-Object FullName, Length
Get-FileHash -LiteralPath $ayuZip -Algorithm SHA256
```

Recreate the same-version ZIP with `-Force`, never `-Update`, so removed files cannot linger. Inspect the displayed entries: the two executables belong at the root and optional modules under `modules/`, with no enclosing `Release/` directory, PDBs, logs, caches or user data. A build or packaging failure must not be reported as a ready release package. Report the ZIP's absolute path, size and SHA-256 with the build result. Uploading or publishing to GitHub is a separate action requiring user authorization.

## Missing or Changed Environment

If the cache, executable, generator instance or dependencies are missing or inconsistent, inspect the installation and the human setup guide before proposing configuration changes. Do not delete or recreate `out/` automatically, or force a previously recorded generator. For fresh setup, choose the toolchain required by the checked-out source and target architecture; use a matching Native Tools environment when that setup requires one.

The traditional dependency layout places the repository beneath a build root, with 64-bit dependencies in `../win64/Libraries`, 32-bit dependencies in `../Libraries`, and shared tools under the build root's `ThirdParty/`. Verify the actual configured paths rather than assuming this layout or copying another machine's drive letters.

## Locked Build Files

If the build reports C1041, LNK1104, an inaccessible PDB or executable, `access denied`, or `file in use`, stop immediately. Do not retry, delete the file, or attempt a workaround. Ask the user to close AywGram/Telegram and any debugger, then wait for confirmation before rebuilding.

After a build, report the command, configuration, architecture, artifact path, verification performed, source changes required, and remaining warnings. Existing artifacts and cache entries alone do not prove the current source builds.
