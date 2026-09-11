import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tarfile
import time
import urllib.request
import zipfile


ROOT = Path(__file__).resolve().parents[2]
STATE = ROOT / "out/ci-cache"
TOOLS = ROOT / "out/ci-tools"
ASSETS = {
    "windows-arm64": (
        "mozilla/sccache", "v0.17.0", "sccache-v0.17.0-aarch64-pc-windows-msvc.zip",
        "82994d1bc92ccc0556f7e6e0ad6cbd08a41a1e84b461fcae628ac2afc8c372bf", "sccache.exe",
    ),
    "windows-x64": (
        "mozilla/sccache", "v0.17.0", "sccache-v0.17.0-x86_64-pc-windows-msvc.zip",
        "e94cfc5b58cbe439302f586c1d1bd7980c2cd371d47bdf385ade657411e6f3ac", "sccache.exe",
    ),
    "mac-arm64": (
        "ccache/ccache", "v4.13.6", "ccache-4.13.6-darwin.tar.gz",
        "0274210ec9c9936ed5711d59b0de3167a51216a588ddde35f6bc828f366fe6d9", "ccache",
    ),
    "linux-x86_64": (
        "ccache/ccache", "v4.13.6", "ccache-4.13.6-linux-x86_64-glibc.tar.xz",
        "508b2a1217dc6e04a23e967c7b95a0fb45d8a7e16fde9e180919698f2e2be060", "ccache",
    ),
}


def capture(*command):
    return subprocess.check_output(command, cwd=ROOT, text=True).strip()


def append_env_file(name, values):
    with open(os.environ[name], "a", encoding="utf-8", newline="\n") as output:
        for key, value in values.items():
            if "\n" in str(value) or "\r" in str(value):
                raise ValueError(f"Multiline value for {key}")
            output.write(f"{key}={value}\n")


def write_json(name, value):
    STATE.mkdir(parents=True, exist_ok=True)
    (STATE / name).write_text(json.dumps(value, indent=2), encoding="utf-8")


def dependency_hash(root, platform):
    patterns = (["Telegram/build/docker/centos_env"] if platform == "linux" else
        ["Telegram/build/prepare", "Telegram/build/qt_version.py"])
    tracked = subprocess.check_output(
        ["git", "ls-files", "-z", "--", *patterns], cwd=root,
    ).decode().split("\0")
    required = (["Telegram/build/docker/centos_env/Dockerfile"] if platform == "linux" else
        ["Telegram/build/prepare/prepare.py", "Telegram/build/qt_version.py"])
    if not all(name in tracked for name in required):
        raise ValueError("Required dependency inputs are missing from the source checkout")
    digest = hashlib.sha256()
    for name in sorted(filter(None, tracked)):
        path = Path(name)
        if any(part in {".venv", "venv", "__pycache__"} for part in path.parts):
            continue
        if path.suffix in {".pyc", ".pyo", ".tmp"}:
            continue
        digest.update(name.encode() + b"\0")
        digest.update((root / path).read_bytes())
        digest.update(b"\0")
    return digest.hexdigest()


def make_keys(platform, arch, toolchain, dependencies, run_id, attempt):
    fingerprint = hashlib.sha256(toolchain.encode()).hexdigest()
    prefix = f"ayw-ci-v3-{platform}-{arch}-{fingerprint}"
    engine = "sccache-0.17.0" if platform == "windows" else "ccache-4.13.6"
    app_prefix = f"{prefix}-app-{engine}-Release-{dependencies}-"
    return {
        "prefix": prefix,
        "dependencies": dependencies,
        "app-prefix": app_prefix,
        "app-key": f"{app_prefix}{run_id}-{attempt}",
    }


def keys(args):
    supported = {"windows": {"arm64", "x64"}, "mac": {"arm64"}, "linux": {"x86_64"}}
    if args.arch not in supported[args.platform]:
        raise ValueError(f"Unsupported cache target: {args.platform}-{args.arch}")
    dependencies = dependency_hash(ROOT, args.platform)
    if args.platform == "windows":
        actual = os.environ.get("VSCMD_ARG_TGT_ARCH", "").lower()
        if actual != args.arch:
            raise ValueError(f"Native Tools target {actual!r} does not match {args.arch}")
        msvc = os.environ["VCToolsVersion"].rstrip("\\/")
        sdk = os.environ["WindowsSDKVersion"].rstrip("\\/")
        if sdk != os.environ["SDK"]:
            raise ValueError(f"Selected SDK {sdk} differs from requested SDK")
        compiler = shutil.which("cl")
        if not compiler or Path(compiler).parent.name.lower() != args.arch:
            raise ValueError(f"Unexpected MSVC compiler path: {compiler}")
        toolchain = f"MSVC {msvc}\nSDK {sdk}\n{capture('cmake', '--version')}"
    elif args.platform == "mac":
        toolchain = "\n".join([
            capture("xcodebuild", "-version"),
            capture("xcrun", "--sdk", "macosx", "--show-sdk-version"),
            capture("xcrun", "clang", "--version"),
            capture("cmake", "--version"),
        ])
    else:
        toolchain = f"Rocky Linux 8 x86_64\nDocker context {dependencies}"
    values = make_keys(args.platform, args.arch, toolchain, dependencies,
        os.environ["GITHUB_RUN_ID"], os.environ["GITHUB_RUN_ATTEMPT"])
    append_env_file("GITHUB_OUTPUT", values)
    cache_directory = Path(os.environ["RUNNER_TEMP"]) / "ayw-app-cache"
    cache_directory.mkdir(parents=True, exist_ok=True)
    environment = ({"SCCACHE_DIR": str(cache_directory), "SCCACHE_BASEDIRS": str(ROOT.parent)}
        if args.platform == "windows" else {"CCACHE_DIR": str(cache_directory)})
    append_env_file("GITHUB_ENV", environment)
    write_json("keys.json", {**values, "toolchain": toolchain})
    print(toolchain)


def install(args):
    repo, version, asset, expected, binary = ASSETS[args.target]
    TOOLS.mkdir(parents=True, exist_ok=True)
    archive = TOOLS / asset
    urllib.request.urlretrieve(
        f"https://github.com/{repo}/releases/download/{version}/{asset}", archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != expected:
        raise ValueError(f"SHA-256 mismatch for {asset}")
    destination = TOOLS / binary
    if asset.endswith(".zip"):
        with zipfile.ZipFile(archive) as package:
            members = [name for name in package.namelist() if Path(name).name == binary]
            if len(members) != 1:
                raise ValueError(f"Expected one {binary} in {asset}")
            destination.write_bytes(package.read(members[0]))
    else:
        with tarfile.open(archive) as package:
            members = [item for item in package.getmembers()
                if item.isfile() and Path(item.name).name == binary]
            if len(members) != 1:
                raise ValueError(f"Expected one {binary} in {asset}")
            destination.write_bytes(package.extractfile(members[0]).read())
    destination.chmod(0o755)
    with open(os.environ["GITHUB_PATH"], "a", encoding="utf-8") as output:
        output.write(str(TOOLS) + "\n")
    archive.unlink()
    print(capture(str(destination), "--version"))


def mac_wrappers(args):
    TOOLS.mkdir(parents=True, exist_ok=True)
    cache = (TOOLS / "ccache").as_posix()
    values = {}
    for language, compiler in (("C", "clang"), ("CXX", "clang++")):
        real = capture("xcrun", "--find", compiler)
        wrapper = TOOLS / f"cached-{compiler}"
        wrapper.write_text(
            '#!/bin/sh\n'
            'unset IPHONEOS_DEPLOYMENT_TARGET IOS_SIMULATOR_DEPLOYMENT_TARGET\n'
            'unset TVOS_DEPLOYMENT_TARGET WATCHOS_DEPLOYMENT_TARGET\n'
            'unset XROS_DEPLOYMENT_TARGET DRIVERKIT_DEPLOYMENT_TARGET\n'
            f'exec {shlex.quote(cache)} {shlex.quote(real)} "$@"\n',
            encoding="utf-8", newline="\n")
        wrapper.chmod(0o755)
        values[f"CACHE_{language}_WRAPPER"] = str(wrapper)
        values[f"REAL_{language}_COMPILER"] = real
    append_env_file("GITHUB_ENV", values)


def parse_stats(engine, raw):
    if engine == "sccache":
        data = json.loads(raw)["stats"]
        writes = data["cache_writes"]
        if not isinstance(writes, int) or writes < 0:
            raise ValueError("Invalid sccache cache_writes")
        return writes, data
    data = {}
    for line in raw.splitlines():
        parts = line.split()
        if len(parts) == 2:
            data[parts[0]] = int(parts[1])
    writes = data["local_storage_write"]
    if data["cache_miss"] < 0 or writes < 0:
        raise ValueError("Invalid ccache counters")
    return writes, data


def stats(args):
    command = (["sccache", "--show-stats", "--stats-format", "json"]
        if args.engine == "sccache" else ["ccache", "--print-stats"])
    try:
        raw = capture(*command)
        writes, data = parse_stats(args.engine, raw)
        write_json("stats.json", {"engine": args.engine, "counters": data})
        append_env_file("GITHUB_OUTPUT", {"changed": str(writes > 0).lower()})
        print(raw)
    except (ValueError, KeyError, subprocess.CalledProcessError) as error:
        write_json("stats.json", {"error": str(error)})
        raise
    finally:
        if args.engine == "sccache":
            subprocess.run(["sccache", "--stop-server"], check=True)


def mark(args):
    path = STATE / "timing.json"
    values = json.loads(path.read_text()) if path.exists() else {}
    values.setdefault(args.phase, {})["end" if args.end else "start"] = time.time()
    write_json("timing.json", values)


def summary(args):
    lines = ["## CI cache", "",
        f"- Workflow ref / cache scope: `{os.environ.get('GITHUB_REF', 'unknown')}`",
        f"- Release tag: `{os.environ.get('RELEASE_TAG') or '(none)'}`"]
    try:
        lines.append(f"- Source commit: `{capture('git', 'rev-parse', 'HEAD')}`")
    except (OSError, subprocess.CalledProcessError):
        lines.append("- Source commit: unavailable")
    lines.append("")
    path = STATE / "keys.json"
    if path.exists():
        values = json.loads(path.read_text())
        lines.extend(["Toolchain:", "```text", values["toolchain"], "```", ""])
    lines.extend(["| Layer | Requested key | Restored key | Match |",
        "| --- | --- | --- | --- |"])
    for entry in json.loads(os.environ.get("CACHE_RESULTS", "[]")):
        restored = entry.get("restored") or ""
        match = "exact" if entry.get("hit") == "true" else "fallback" if restored else "miss / skipped"
        lines.append(f"| {entry['layer']} | `{entry.get('key', '')}` | `{restored}` | {match} |")
    path = STATE / "timing.json"
    if path.exists():
        for phase, values in json.loads(path.read_text()).items():
            seconds = values.get("end", time.time()) - values["start"]
            status = "completed" if "end" in values else "incomplete, elapsed"
            lines.append(f"\n{phase}: {seconds / 60:.1f} min ({status}).")
    path = STATE / "stats.json"
    if path.exists():
        lines.extend(["", "Compiler cache counters (including uncacheable reasons):",
            "```json", path.read_text(), "```"])
    else:
        lines.append("\nCompiler cache statistics unavailable or caching disabled.")
    for variable in ("SCCACHE_DIR", "CCACHE_DIR"):
        if os.environ.get(variable):
            directory = Path(os.environ[variable])
            size = sum(item.stat().st_size for item in directory.rglob("*") if item.is_file())
            lines.append(f"\n{variable}: {size / (1024 ** 2):.1f} MiB (1 GiB limit).")
    if os.environ.get("RUNNER_OS") == "Linux":
        directories = [Path(os.environ["RUNNER_TEMP"]) / name
            for name in (".buildx-cache", ".mount-cache")]
    else:
        directories = [Path(os.environ.get("LibrariesPath", ROOT.parent / "Libraries")),
            ROOT.parent / "ThirdParty"]
    for directory in directories:
        size = sum(item.stat().st_size for item in directory.rglob("*") if item.is_file())
        lines.append(f"\n{directory.name}: {size / (1024 ** 2):.1f} MiB on runner (before archive compression).")
    if os.environ.get("RUNNER_OS") == "Windows":
        lines.append("\nMSVC PCH remains enabled; PCH compilations bypass sccache.")
    try:
        usage = json.loads(capture("gh", "api",
            f"repos/{os.environ['GITHUB_REPOSITORY']}/actions/cache/usage"))
        size = usage["active_caches_size_in_bytes"] / (1024 ** 3)
        lines.append(f"\nRepository cache usage: {size:.2f} GiB, {usage['active_caches_count']} entries.")
        if size >= 8:
            lines.append("\nCache usage is near the default 10 GiB allowance; old snapshots may be evicted.")
    except (subprocess.CalledProcessError, ValueError, KeyError):
        lines.append("\nRepository cache usage unavailable (API permissions or service response).")
    lines.append("\nReleases dispatched on the default branch and manual prewarming share compatible caches. Unused caches can expire after 7 days; old snapshots also compete for repository storage.")
    with open(os.environ["GITHUB_STEP_SUMMARY"], "a", encoding="utf-8") as output:
        output.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    key_parser = commands.add_parser("keys")
    key_parser.add_argument("platform", choices=["windows", "mac", "linux"])
    key_parser.add_argument("arch", choices=["arm64", "x64", "x86_64"])
    install_parser = commands.add_parser("install")
    install_parser.add_argument("target", choices=ASSETS)
    commands.add_parser("mac-wrappers")
    stats_parser = commands.add_parser("stats")
    stats_parser.add_argument("engine", choices=["sccache", "ccache"])
    mark_parser = commands.add_parser("mark")
    mark_parser.add_argument("phase", choices=["dependencies", "application"])
    mark_parser.add_argument("--end", action="store_true")
    commands.add_parser("summary")
    args = parser.parse_args()
    globals()[args.command.replace("-", "_")](args)


if __name__ == "__main__":
    main()
