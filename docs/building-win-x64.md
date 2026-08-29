# Windows x64 构建

本文档根据 Telegram Desktop 上游 Windows 构建说明翻译并针对本仓库的 x64 Release 构建进行了补充。

- [准备目录与工具](#准备目录与工具)
- [初始化 Visual Studio 环境](#初始化-visual-studio-环境)
- [克隆源码并准备依赖](#克隆源码并准备依赖)
- [配置并构建项目](#配置并构建项目)
- [Qt Visual Studio Tools](#qt-visual-studio-tools)

## 准备目录与工具

Windows 构建使用 **Visual Studio 2026** 和 **10.0.26100.0** Windows SDK。

选择一个空目录作为构建根目录（下文称为 `BuildPath`），例如 `D:\TBuild`。仓库、
`Libraries` 和 `ThirdParty` 必须位于这个根目录下：

```text
BuildPath\
├─ AyuGramDesktop\
├─ Libraries\
└─ ThirdParty\
```

安装以下工具：

- Python 3.10，并将其加入 `PATH`。
- Git for Windows。
- Visual Studio 2026 的“使用 C++ 的桌面开发”工作负载、MSVC 14.44 x86/x64 ATL，以及
  Windows 11 SDK 10.0.26100.0。

构建还需要 Telegram API 的 `api_id` 和 `api_hash`，获取方法见
[Telegram API 凭据](api_credentials.md)。

## 初始化 Visual Studio 环境

Visual Studio 2026 默认的 `v145` 工具集不支持 Windows 7。Telegram Desktop 需要显式选择
MSVC 14.44（`v144.4`，基于仍支持 Windows 7 的 `v143`）。在普通命令提示符中运行：

```bat
%comspec% /k "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
```

后续准备依赖、配置和构建命令都必须在这个已初始化的终端中执行。可使用以下命令确认环境：

```bat
cl
echo %VCToolsVersion%
```

`VCToolsVersion` 应以 `14.44` 开头。

## 克隆源码并准备依赖

在 `BuildPath` 中递归克隆本仓库：

```bat
cd /d D:\TBuild
git clone --recursive <本仓库地址> AyuGramDesktop
```

本仓库当前固定使用 Qt 6.11.1。只准备 x64 Release 构建所需的 Qt 6 和第三方库：

```bat
AyuGramDesktop\Telegram\build\prepare\win.bat skip-debug qt6
```

`prepare` 会下载 Qt 源码和其他依赖、应用 Telegram 补丁，并将静态 Qt 安装到
`BuildPath\Libraries\win64\Qt-6.11.1`。Qt Online Installer 和系统级 Qt 安装不是必需项。

准备完成后不要单独移动仓库；构建脚本和 CMake 缓存会记录 `BuildPath` 下的绝对路径。

## 配置并构建项目

进入仓库的 `Telegram` 目录，使用自己的 Telegram API 凭据配置 x64 工程：

```bat
cd /d D:\TBuild\AyuGramDesktop\Telegram
configure.bat x64 qt6 -D TDESKTOP_API_ID=YOUR_API_ID -D TDESKTOP_API_HASH=YOUR_API_HASH
```

可以在 Visual Studio 2026 中打开 `BuildPath\AyuGramDesktop\out\Telegram.slnx`，选择
`Telegram` 目标和 Release 配置；也可以直接使用 Visual Studio 随附的 CMake：

```bat
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build ..\out --config Release --target Telegram
```

构建产物位于 `BuildPath\AyuGramDesktop\out\Release\AywGram.exe`。不要提交 `out`、
`Libraries`、`ThirdParty` 或其中的缓存文件。

## Qt Visual Studio Tools

如需改善 Visual Studio 中的 Qt 调试体验，可以打开 **扩展 > 管理扩展**，搜索并安装
**Qt Visual Studio Tools**。它只提供 IDE 集成，不负责本仓库所需 Qt 的下载和构建。
