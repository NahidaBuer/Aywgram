# Agent Build Guide: macOS

Read this file only for macOS builds. Human setup instructions are in the
[macOS build guide](building-mac.md).

macOS builds require Xcode and dependencies under
`../Libraries/local/Qt-*`. The Qt version is selected by
`Telegram/build/qt_version.py`; do not hardcode or manually override it unless
diagnosis requires a deliberate version change.

Keep `DEVELOPER_DIR`, dependency preparation, configure, and build on the same
Xcode installation. Before reusing `out/`, verify its generator, Release
configuration, target architecture, deployment target, and existing
artifacts.

For an already configured tree, run from the repository root:

```bash
cmake --build out --config Release --target Telegram
```

## Single-architecture Release artifacts

A successful CMake/Xcode Release build is not yet a size-ready artifact. For
an explicitly requested single-architecture build:

1. Capture any required symbols before stripping the only copy of a binary.
2. Strip the main executable and each bundled project helper.
3. Inspect every Mach-O in the application bundle with `file` or
   `lipo -archs`.
4. Remove every foreign architecture slice. An ARM64-only artifact must not
   retain `x86_64` or universal Mach-O files.
5. Re-sign the modified local bundle ad hoc with
   `Telegram/Telegram/Telegram.entitlements`.
6. Run `codesign --verify --deep --strict` and repeat the complete Mach-O
   architecture scan.

`swift-stdlib-tool` may copy an x86-only `libswiftCore.dylib` and a universal
`libswift_Concurrency.dylib` even when the application executable is thin
ARM64. Confirm the executable does not load the bundled Swift Core before
removing that incompatible copy, and use `lipo -thin arm64` to replace the
Concurrency library with its ARM64 slice.

`strip`, `lipo`, and removing bundled libraries invalidate the current code
signature. Do not use a developer identity or notarize unless the user
explicitly requests it. Apply all packaging operations only below `out/`, and
preserve a recoverable original bundle or rely on rebuilding it.

After a build, report the Xcode selection, command, configuration,
architecture, artifact path, signing and architecture verification, source
changes required, and remaining warnings.
