# Agent Build Guide: Linux

Read this file only for native Linux or WSL/Linux builds. Human setup
instructions are in the [Linux build guide](building-linux.md).

Linux dependencies are normally under `../Libraries`. Set `QT` only when the
selected environment requires it; prefer the version chosen by the repository
scripts. Verify the generator, target platform, architecture, and existing
artifacts before reusing `out/`.

For an already configured native tree, run from the repository root:

```bash
cmake --build out --config Release --target Telegram
```

## WSL checkout

A checkout opened through a Windows UNC path such as
`\\wsl.localhost\{distro}\home\{user}\Telegram\tdesktop` remains a Linux
checkout whose real path is `/home/{user}/Telegram/tdesktop`.

- Prefer repository-aware commands through WSL:

  ```powershell
  wsl.exe -d {distro} --cd /home/{user}/Telegram/tdesktop -- <command>
  ```

- Native Windows tools may observe different ownership, paths, executable
  bits, or line endings through the UNC path.
- Use WSL Git if Windows Git reports dubious ownership. Do not change global
  `safe.directory` settings unless the user asks.
- Match path syntax to the shell. Use Linux paths with WSL commands and quoted
  UNC paths with native Windows commands.
- If a command behaves unexpectedly from PowerShell, retry it through WSL
  before treating the repository as broken.
- Keep the checkout LF-only unless a file already uses another convention. Do
  not let native Windows tools add CRLF or a UTF-8 BOM.
- When using `task-think` from WSL, keep `.ai/` artifacts and edited project
  files LF-only. Its Windows normalization phase applies only to native Windows
  checkouts.

Do not assume WSL has the native build toolchain. Inspect existing `out/`
artifacts before deciding which platform configured them, and never run native
Windows CMake against a Linux/Docker build tree.

From WSL, use the repository Docker environment rather than native Windows
CMake. The Docker daemon must be reachable; checking `docker info` is allowed,
but starting the build still requires user authorization:

```bash
Telegram/build/docker/centos_env/run.sh bash -lc "cd /usr/src/tdesktop && cmake --build out --config Release --target Telegram"
```

Existing output such as `out/Release/Telegram` may be an ELF executable, not
`Telegram.exe`. Inspect it before selecting tools or reporting the artifact.

If libraries are missing, verify the repository and dependency layout before
changing CMake files. Repair ordinary configure, compile, and link failures
only within the authorized scope.

After a build, report the command, configuration, architecture, artifact path,
verification performed, source changes required, and remaining warnings.
