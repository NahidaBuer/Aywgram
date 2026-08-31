#!/usr/bin/env python3

import argparse
import datetime
import hashlib
import json
import pathlib
import posixpath
import re
import sys
import tarfile
import urllib.parse
import zipfile


SCHEMA = 1
TARGET_FORMATS = {
    "windows-x86_64": "zip",
    "windows-arm64": "zip",
    "mac-arm64": "zip",
    "linux-x86_64": "tar.gz",
}
TARGET_NAMES = {
    "windows-x86_64": "Windows x86_64",
    "windows-arm64": "Windows ARM64",
    "mac-arm64": "macOS ARM64",
    "linux-x86_64": "Linux x86_64",
}
MAX_ARCHIVE_SIZE = 1024 * 1024 * 1024
MAX_EXPANDED_SIZE = 4 * 1024 * 1024 * 1024
MAX_METADATA_SIZE = 64 * 1024
HASH_PATTERN = re.compile(r"[0-9a-f]{64}")
APP_VERSION_PATTERN = re.compile(r"[1-9][0-9]*")
RELEASE_PATTERN = re.compile(r"[0-9A-Za-z][0-9A-Za-z._-]*")
VERSION_NAME_PATTERN = re.compile(
    r"(?P<base>(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)"
    r"(?:\.(?:0|[1-9][0-9]*))?)-(?P<revision>[1-9]|[1-9][0-9])"
)


class MetadataError(ValueError):
    pass


def read_json(path):
    source_path = pathlib.Path(path)
    if source_path.stat().st_size > MAX_METADATA_SIZE:
        raise MetadataError(f"JSON input exceeds {MAX_METADATA_SIZE} bytes")
    with source_path.open("r", encoding="utf-8") as source:
        return json.load(source)


def write_json(path, value):
    destination = pathlib.Path(path)
    destination.parent.mkdir(parents=True, exist_ok=True)
    encoded = (json.dumps(
        value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")
    if len(encoded) > MAX_METADATA_SIZE:
        raise MetadataError(f"JSON output exceeds {MAX_METADATA_SIZE} bytes")
    destination.write_bytes(encoded)


def validate_release(value):
    if not isinstance(value, str) or not RELEASE_PATTERN.fullmatch(value):
        raise MetadataError("release must be a safe GitHub tag")
    return value


def validate_asset(target, value):
    if not isinstance(value, dict):
        raise MetadataError(f"target {target} must be an object")
    app_version = value.get("app_version")
    if (type(app_version) is not int or app_version <= 0
            or app_version > 2147483647):
        raise MetadataError(f"target {target} has an invalid app_version")
    revision = value.get("revision")
    if type(revision) is not int or revision < 1 or revision > 99:
        raise MetadataError(f"target {target} has an invalid revision")
    version_name = value.get("version_name")
    version_match = (VERSION_NAME_PATTERN.fullmatch(version_name)
                     if isinstance(version_name, str) else None)
    if not version_match:
        raise MetadataError(f"target {target} has an invalid version_name")
    parts = [int(part) for part in version_match.group("base").split(".")]
    if len(parts) == 2:
        parts.append(0)
    computed_version = parts[0] * 1000000 + parts[1] * 1000 + parts[2]
    if (any(part > 999 for part in parts)
            or computed_version != app_version
            or int(version_match.group("revision")) != revision):
        raise MetadataError(
            f"target {target} version fields do not describe one release")
    release = validate_release(value.get("release"))
    if release != f"pre-release-v{version_name}":
        raise MetadataError(f"target {target} release does not match version_name")
    url = value.get("url")
    if not isinstance(url, str):
        raise MetadataError(f"target {target} has an invalid url")
    parsed = urllib.parse.urlparse(url)
    prefix = f"/NahidaBuer/AywGram/releases/download/{release}/"
    if (parsed.scheme != "https" or parsed.hostname != "github.com"
            or parsed.port is not None or parsed.username is not None):
        raise MetadataError(f"target {target} url must use https://github.com")
    suffix = parsed.path[len(prefix):] if parsed.path.startswith(prefix) else ""
    if (not suffix or "/" in suffix or parsed.query or parsed.fragment):
        raise MetadataError(f"target {target} url is outside its release")
    archive_format = value.get("format")
    if archive_format != TARGET_FORMATS[target]:
        raise MetadataError(f"target {target} must use {TARGET_FORMATS[target]}")
    extension = "zip" if archive_format == "zip" else "tar.gz"
    expected_name = f"AywGram-v{version_name}-{target}.{extension}"
    if suffix != expected_name:
        raise MetadataError(f"target {target} has an unexpected asset name")
    size = value.get("size")
    if type(size) is not int or size <= 0 or size > MAX_ARCHIVE_SIZE:
        raise MetadataError(f"target {target} has an invalid size")
    sha256 = value.get("sha256")
    if not isinstance(sha256, str) or not HASH_PATTERN.fullmatch(sha256):
        raise MetadataError(f"target {target} has an invalid sha256")
    return {
        "app_version": app_version,
        "revision": revision,
        "version_name": version_name,
        "release": release,
        "url": url,
        "format": archive_format,
        "size": size,
        "sha256": sha256,
    }


def validate_metadata(value):
    if (not isinstance(value, dict) or type(value.get("schema")) is not int
            or value.get("schema") != SCHEMA):
        raise MetadataError("unsupported metadata schema")
    feed_release = validate_release(value.get("feed_release"))
    generated_at = value.get("generated_at")
    if not isinstance(generated_at, str) or not generated_at.endswith("Z"):
        raise MetadataError("generated_at must be a UTC timestamp")
    try:
        timestamp = datetime.datetime.fromisoformat(generated_at[:-1] + "+00:00")
    except ValueError as error:
        raise MetadataError("generated_at must be a UTC timestamp") from error
    if timestamp.utcoffset() != datetime.timedelta(0):
        raise MetadataError("generated_at must be a UTC timestamp")
    targets = value.get("targets")
    if not isinstance(targets, dict):
        raise MetadataError("targets must be an object")
    checked = {}
    for target, asset in targets.items():
        if target not in TARGET_FORMATS:
            continue
        checked[target] = validate_asset(target, asset)
    return {
        "schema": SCHEMA,
        "feed_release": feed_release,
        "generated_at": generated_at,
        "targets": checked,
    }


def sha256_file(path):
    digest = hashlib.sha256()
    with pathlib.Path(path).open("rb") as source:
        while chunk := source.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


def valid_member_name(name):
    normalized = name.replace("\\", "/")
    if not normalized or normalized.startswith("/") or ":" in normalized:
        return False
    parts = [part for part in normalized.split("/") if part not in ("", ".")]
    if not parts or ".." in parts:
        return False
    if parts[0].lower() == "tdata":
        return False
    return parts[-1].lower() not in ("ready", "update-metadata.json")


def validate_zip(path, target):
    try:
        with zipfile.ZipFile(path) as archive:
            members = archive.infolist()
            if not members:
                raise MetadataError("zip archive is empty")
            expanded = 0
            files = {}
            kinds = {}
            names = set()
            for member in members:
                if not valid_member_name(member.filename):
                    raise MetadataError(f"unsafe zip member: {member.filename}")
                normalized = posixpath.normpath(
                    member.filename.replace("\\", "/")).rstrip("/")
                key = normalized.casefold()
                if key in names:
                    raise MetadataError(f"duplicate zip member: {member.filename}")
                names.add(key)
                expanded += member.file_size
                if expanded > MAX_EXPANDED_SIZE:
                    raise MetadataError("zip archive expands beyond the limit")
                mode = member.external_attr >> 16
                kind = mode & 0o170000
                if kind == 0o120000:
                    if not target.startswith("mac-"):
                        raise MetadataError(f"unexpected zip symlink: {member.filename}")
                    link = archive.read(member).decode("utf-8")
                    resolved = posixpath.normpath(posixpath.join(
                        posixpath.dirname(normalized), link))
                    if (not link or link.startswith("/") or resolved == ".."
                            or resolved.startswith("../")):
                        raise MetadataError(f"unsafe zip symlink: {member.filename}")
                elif kind not in (0, 0o040000, 0o100000):
                    raise MetadataError(f"unexpected zip member type: {member.filename}")
                if not member.is_dir():
                    files[normalized] = mode
                    kinds[normalized] = kind
    except (OSError, UnicodeDecodeError, zipfile.BadZipFile) as error:
        raise MetadataError("invalid zip archive") from error
    if target.startswith("windows-"):
        required = ("AywGram.exe", "Updater.exe")
        for name in names:
            if "/" in name and not name.startswith("modules/"):
                raise MetadataError(f"unexpected Windows archive root: {name}")
    else:
        required = (
            "AywGram.app/Contents/MacOS/AywGram",
            "AywGram.app/Contents/Frameworks/Updater",
        )
        if any(name != "aywgram.app"
               and not name.startswith("aywgram.app/") for name in names):
            raise MetadataError("mac archive must contain only AywGram.app")
    for name in required:
        if name not in files:
            raise MetadataError(f"archive is missing {name}")
        if target.startswith("mac-") and kinds[name] == 0o120000:
            raise MetadataError(f"archive member must be a file: {name}")
        if target.startswith("mac-") and not (files[name] & 0o111):
            raise MetadataError(f"archive member is not executable: {name}")


def validate_tar(path):
    try:
        with tarfile.open(path, mode="r:gz") as archive:
            members = archive.getmembers()
            if not members:
                raise MetadataError("tar archive is empty")
            expanded = 0
            files = {}
            names = set()
            for member in members:
                if not valid_member_name(member.name):
                    raise MetadataError(f"unsafe tar member: {member.name}")
                normalized = posixpath.normpath(
                    member.name.replace("\\", "/")).rstrip("/")
                if normalized in names:
                    raise MetadataError(f"duplicate tar member: {member.name}")
                names.add(normalized)
                if not member.isfile() and not member.isdir():
                    raise MetadataError(f"unexpected tar member type: {member.name}")
                expanded += member.size
                if expanded > MAX_EXPANDED_SIZE:
                    raise MetadataError("tar archive expands beyond the limit")
                if member.isfile():
                    files[normalized] = member.mode
    except (OSError, tarfile.TarError) as error:
        raise MetadataError("invalid tar archive") from error
    for name in ("AywGram", "Updater"):
        if name not in files:
            raise MetadataError(f"archive is missing {name}")
        if not (files[name] & 0o111):
            raise MetadataError(f"archive member is not executable: {name}")
    if names != {"AywGram", "Updater"}:
        raise MetadataError("Linux archive must contain only AywGram and Updater")


def validate_archive(path, target):
    asset = pathlib.Path(path)
    size = asset.stat().st_size
    if size <= 0 or size > MAX_ARCHIVE_SIZE:
        raise MetadataError("archive size is outside the allowed range")
    if TARGET_FORMATS[target] == "zip":
        validate_zip(asset, target)
    else:
        validate_tar(asset)


def create_fragment(args):
    target = args.target
    if target not in TARGET_FORMATS:
        raise MetadataError(f"unsupported target: {target}")
    if not APP_VERSION_PATTERN.fullmatch(args.app_version):
        raise MetadataError("app_version must be a positive integer")
    if not args.revision.isdigit():
        raise MetadataError("revision must be an integer")
    release = validate_release(args.release)
    asset = pathlib.Path(args.asset)
    validate_archive(asset, target)
    size = asset.stat().st_size
    name = asset.name
    url = (
        f"https://github.com/NahidaBuer/AywGram/releases/download/"
        f"{urllib.parse.quote(release, safe='')}/"
        f"{urllib.parse.quote(name)}"
    )
    fragment = {
        "target": target,
        "asset": validate_asset(target, {
            "app_version": int(args.app_version),
            "revision": int(args.revision),
            "version_name": args.version_name,
            "release": release,
            "url": url,
            "format": TARGET_FORMATS[target],
            "size": size,
            "sha256": sha256_file(asset),
        }),
    }
    write_json(args.output, fragment)


def merge_metadata(args):
    if args.base and pathlib.Path(args.base).exists():
        metadata = validate_metadata(read_json(args.base))
    else:
        metadata = {
            "schema": SCHEMA,
            "feed_release": validate_release(args.feed_release),
            "generated_at": args.generated_at,
            "targets": {},
        }
    incoming = {}
    for path in args.fragment:
        fragment = read_json(path)
        target = fragment.get("target") if isinstance(fragment, dict) else None
        if target not in TARGET_FORMATS:
            raise MetadataError(f"invalid target fragment: {path}")
        if target in incoming:
            raise MetadataError(f"duplicate target fragment: {target}")
        incoming[target] = validate_asset(target, fragment.get("asset"))
    for target, asset in incoming.items():
        previous = metadata["targets"].get(target)
        asset_version = (asset["app_version"], asset["revision"])
        previous_version = ((previous["app_version"], previous["revision"])
                            if previous else None)
        if previous and asset_version < previous_version:
            raise MetadataError(f"target {target} would roll back")
        if previous and asset_version == previous_version:
            old_name = urllib.parse.urlparse(previous["url"]).path.rsplit("/", 1)[-1]
            new_name = urllib.parse.urlparse(asset["url"]).path.rsplit("/", 1)[-1]
            if asset["release"] != previous["release"] or new_name != old_name:
                raise MetadataError(
                    f"target {target} rebuild changed release or asset name")
        metadata["targets"][target] = asset
    metadata["feed_release"] = validate_release(args.feed_release)
    metadata["generated_at"] = args.generated_at
    write_json(args.output, metadata)


def render_release_notes(args):
    metadata = validate_metadata(read_json(args.metadata))
    lines = [
        "AywGram 采用按平台独立推进的滚动发布模式。此 Release 是固定下载入口，应用归档实际存放在对应的 prerelease。",
        "",
        "| 平台 | 当前版本 | 下载 |",
        "| --- | --- | --- |",
    ]
    for target, name in TARGET_NAMES.items():
        asset = metadata["targets"].get(target)
        if not asset:
            continue
        size = asset["size"] / (1024 * 1024)
        archive_format = "ZIP" if asset["format"] == "zip" else "tar.gz"
        lines.append(
            f"| {name} | `{asset['version_name']}` | "
            f"[下载 {archive_format}（{size:.1f} MiB）]({asset['url']}) |"
        )
    lines.extend([
        "",
        f"Metadata 更新时间：`{metadata['generated_at']}`。自动更新协议和归档布局见 [项目文档](https://github.com/NahidaBuer/AywGram/blob/main/docs/aywgram-updater.md)。",
        "",
    ])
    destination = pathlib.Path(args.output)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(lines), encoding="utf-8")


def parser():
    result = argparse.ArgumentParser()
    commands = result.add_subparsers(dest="command", required=True)
    fragment = commands.add_parser("fragment")
    fragment.add_argument("--target", required=True)
    fragment.add_argument("--app-version", required=True)
    fragment.add_argument("--revision", required=True)
    fragment.add_argument("--version-name", required=True)
    fragment.add_argument("--release", required=True)
    fragment.add_argument("--asset", required=True)
    fragment.add_argument("--output", required=True)
    fragment.set_defaults(run=create_fragment)
    merge = commands.add_parser("merge")
    merge.add_argument("--base")
    merge.add_argument("--feed-release", required=True)
    merge.add_argument(
        "--generated-at",
        default=datetime.datetime.now(datetime.timezone.utc)
        .isoformat(timespec="seconds")
        .replace("+00:00", "Z"),
    )
    merge.add_argument("--fragment", action="append", default=[])
    merge.add_argument("--output", required=True)
    merge.set_defaults(run=merge_metadata)
    notes = commands.add_parser("notes")
    notes.add_argument("--metadata", required=True)
    notes.add_argument("--output", required=True)
    notes.set_defaults(run=render_release_notes)
    return result


def main():
    args = parser().parse_args()
    try:
        args.run(args)
    except (MetadataError, OSError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
