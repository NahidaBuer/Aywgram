# Agent Build Guide: Windows

Read this file only for native Windows builds. Human setup instructions are
in the [Windows x64 build guide](building-win-x64.md).

A checkout opened through `\\wsl.localhost\...` is a Linux checkout, not a
native Windows checkout. Do not use native Windows CMake on its `out/` tree;
follow the [Linux agent build guide](agent-building-linux.md) instead.

## Native Windows layout and toolchain

The traditional dependency layout is:

```text
L:\Telegram\                    # BuildPath
L:\Telegram\tdesktop\           # Repository
L:\Telegram\Libraries\          # 32-bit dependencies
L:\Telegram\win64\Libraries\    # 64-bit dependencies
L:\Telegram\ThirdParty\         # Build tools
```

Use Visual Studio 2026 and the Native Tools Command Prompt matching the
target: x64 for `win64`, x86 for `win`, and ARM64 for `winarm`.
Dependencies are normally in `../win64/Libraries` for 64-bit builds or
`../Libraries` for 32-bit builds.

For an already configured tree, run from the repository root:

```powershell
$env:CL = '/utf-8'
cmake --build out --config Release --target Telegram
```

If the shell cannot change directories, pass a quoted absolute build path:

```powershell
cmake --build "L:\Telegram\tx64\out" --config Release --target Telegram
```

Verify the generator, target architecture, dependency directory, and existing
artifacts before reusing `out/`.

## Locked build files

If the build reports C1041, LNK1104, an inaccessible PDB or executable,
`access denied`, or `file in use`, stop immediately. Do not retry, delete the
file, or attempt a workaround. Ask the user to close AywGram/Telegram and any
debugger, then wait for confirmation before rebuilding.

After a build, report the command, configuration, architecture, artifact path,
verification performed, source changes required, and remaining warnings.
