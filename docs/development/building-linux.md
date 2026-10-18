# Linux 构建

本流程使用 Docker 构建。先安装 Git、Python、Poetry、Docker 和 Docker Buildx，并确认当前用户可以访问 Docker daemon。

```bash
git clone --recursive <本仓库地址> tdesktop
cd tdesktop
./Telegram/build/prepare/linux.sh
```

## Release 构建

```bash
docker run --rm -it \
  -u "$(id -u)" \
  -v "$PWD:/usr/src/tdesktop" \
  ghcr.io/telegramdesktop/tdesktop/centos_env:latest \
  /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

## Debug 构建

```bash
docker run --rm -it \
  -u "$(id -u)" \
  -v "$PWD:/usr/src/tdesktop" \
  -e CONFIG=Debug \
  ghcr.io/telegramdesktop/tdesktop/centos_env:latest \
  /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

构建树位于 `out/build/linux-x86_64/<configuration>/default/`；可在发布前按需使用 `strip` 缩小二进制体积。API 标识的来源见 [API 凭据说明](api_credentials.md)。不要在提交中包含容器缓存或构建产物。

## Visual Studio Code（可选）

完成准备步骤后，可在 Visual Studio Code 中打开仓库并使用 Dev Containers 扩展。现有 `.devcontainer.json` 假定本机已具备 `tdesktop:centos_env` 镜像；若镜像不可用，请直接使用上方 Docker 命令，或先将开发容器配置改为团队可访问的镜像。

遵循 [out 约定](../agents/out-layout.md)，成功的 AywGram 和 Updater 保留到 `out/artifacts/linux-x86_64/<configuration>/<run-id>/`，从该副本打包到 `out/dist/`，日志与最终报告放在 `out/logs/build/<run-id>/`。
