# Windows x64 构建

以下步骤用于 Windows x64 构建。当前流程使用 Visual Studio 2022 与 Windows SDK
`10.0.26100.0`；依赖和支持版本会随上游调整，请以 `Telegram/build/prepare/win.bat` 的实际
检查结果为准。

## 准备目录与工具

选择一个空的构建根目录，例如 `D:\TBuild`，并在其中创建 `ThirdParty` 与 `Libraries` 目录。
除非另有说明，以下命令都应在 **x64 Native Tools Command Prompt for VS 2022** 中执行。

安装 Git 与 Python 3.10，并确认二者已加入 `PATH`。安装 Visual Studio 时选择 C++ 桌面开发
工作负载、最新的 C++ MFC/ATL（x86 和 x64）以及 Windows 11 SDK。

## 克隆并准备依赖

在构建根目录中递归克隆本仓库：

```bat
cd /d D:\TBuild
git clone --recursive <本仓库地址> tdesktop
tdesktop\Telegram\build\prepare\win.bat
```

准备脚本会下载并配置所需第三方库。它的目录假设与构建根目录有关，因此不要把仓库移动到
准备完成后的其他位置。

## 配置与构建

进入 `D:\TBuild\tdesktop\Telegram`（替换为实际路径）并配置：

```bat
cd /d D:\TBuild\tdesktop\Telegram
configure.bat x64 -D TDESKTOP_API_ID=2040 -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

打开根目录 `out/Telegram.slnx`，选择 `Telegram` 项目并构建 Debug 或 Release；也可在
`Telegram` 目录执行：

```bat
cmake --build ..\out --config Debug --target Telegram
```

产物分别位于根目录的 `out/Debug/` 与 `out/Release/`。不要提交 `out/` 或依赖缓存。
