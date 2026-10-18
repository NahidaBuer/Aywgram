# macOS 构建

使用当前源码和依赖准备脚本要求的 Xcode/SDK，配置、依赖与构建保持同一工具链。完整构建约需要 55 GB 可用空间：双架构依赖约 35 GB，`out/` 中的编译产物约 20 GB。

需要 Xcode 命令行工具、Git、Python、CMake、Ninja、automake、libtool、pkg-config、GNU tar、nasm 和 meson。若尚未安装 Homebrew，可按其官方指引安装；随后安装依赖：

```bash
brew install git automake libtool cmake wget pkg-config gnu-tar ninja nasm meson
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer
```

在任意构建根目录中执行：

```bash
git clone --recursive <本仓库地址> tdesktop
cd tdesktop
./Telegram/build/prepare/mac.sh
cd Telegram
./configure.sh -B "$PWD/../out/build/mac-arm64/Release/default" -D DESKTOP_APP_MAC_ARCH=arm64 -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

配置完成后，用 Xcode 打开`out/build/mac-arm64/Release/default/Telegram.xcodeproj`，选择 Release 配置并构建；Debug 应使用独立配置目录。API 标识的来源见 [API 凭据说明](api_credentials.md)。

脚本、Qt 版本和支持的架构可能随上游变动；遇到问题时先检查 `Telegram/build/prepare/mac.sh` 的要求，而不是套用旧版本的 Xcode 或手工补丁。

输出遵循 [out 约定](../agents/out-layout.md)：保留程序位于 `out/artifacts/<target>/<configuration>/<run-id>/`，ZIP 位于 `out/dist/`，日志与最终报告位于 `out/logs/build/<run-id>/`。剥离、架构裁剪和重新签名在完整程序副本上执行。
