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
```

If the shell cannot change directories, set `$ayuBuildTree` to the verified absolute `out/` path and use that same tree for cache inspection and the build. The target is `Telegram`; this fork's Release executable is `out/Release/AywGram.exe`.

## Missing or Changed Environment

If the cache, executable, generator instance or dependencies are missing or inconsistent, inspect the installation and the human setup guide before proposing configuration changes. Do not delete or recreate `out/` automatically, or force a previously recorded generator. For fresh setup, choose the toolchain required by the checked-out source and target architecture; use a matching Native Tools environment when that setup requires one.

The traditional dependency layout places the repository beneath a build root, with 64-bit dependencies in `../win64/Libraries`, 32-bit dependencies in `../Libraries`, and shared tools under the build root's `ThirdParty/`. Verify the actual configured paths rather than assuming this layout or copying another machine's drive letters.

## Locked Build Files

If the build reports C1041, LNK1104, an inaccessible PDB or executable, `access denied`, or `file in use`, stop immediately. Do not retry, delete the file, or attempt a workaround. Ask the user to close AywGram/Telegram and any debugger, then wait for confirmation before rebuilding.

After a build, report the command, configuration, architecture, artifact path, verification performed, source changes required, and remaining warnings. Existing artifacts and cache entries alone do not prove the current source builds.
