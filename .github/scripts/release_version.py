#!/usr/bin/env python3

import argparse
import pathlib
import re
import sys


BASE_VERSION_EXPRESSION = (
    r"(?:0|[1-9][0-9]*)\."
    r"(?:0|[1-9][0-9]*)(?:\.(?:0|[1-9][0-9]*))?"
)
BASE_VERSION_PATTERN = re.compile(BASE_VERSION_EXPRESSION)
TAG_PATTERN = re.compile(
    rf"pre-release-v(?P<base>{BASE_VERSION_EXPRESSION})-"
    r"(?P<revision>[1-9]|[1-9][0-9])"
)
APP_VERSION_PATTERN = re.compile(
    r"constexpr auto AppVersion = (?P<value>[0-9]+);"
)
APP_VERSION_STR_PATTERN = re.compile(
    r'constexpr auto AppVersionStr = "(?P<value>[^"]+)";'
)
APP_RELEASE_REVISION_PATTERN = re.compile(
    r"constexpr auto AppReleaseRevision = (?P<value>[0-9]+);"
)


class ReleaseVersionError(ValueError):
    pass


def numeric_version(base_version):
    if not BASE_VERSION_PATTERN.fullmatch(base_version):
        raise ReleaseVersionError("base version must use canonical decimal parts")
    parts = base_version.split(".")
    if len(parts) == 2:
        parts.append("0")
    if len(parts) != 3:
        raise ReleaseVersionError("base version must have two or three parts")
    numbers = [int(part) for part in parts]
    if any(number > 999 for number in numbers):
        raise ReleaseVersionError("base version parts must be between 0 and 999")
    return numbers[0] * 1000000 + numbers[1] * 1000 + numbers[2]


def read_source_version(path):
    source = pathlib.Path(path).read_text(encoding="utf-8")
    app_version = APP_VERSION_PATTERN.search(source)
    app_version_str = APP_VERSION_STR_PATTERN.search(source)
    revision = APP_RELEASE_REVISION_PATTERN.search(source)
    if not app_version or not app_version_str or not revision:
        raise ReleaseVersionError("version header is missing release constants")
    return source, {
        "app_version": int(app_version.group("value")),
        "base_version_name": app_version_str.group("value"),
        "revision": int(revision.group("value")),
    }


def resolve_version(tag, path):
    source, current = read_source_version(path)
    expected = numeric_version(current["base_version_name"])
    if expected != current["app_version"]:
        raise ReleaseVersionError(
            f"AppVersionStr {current['base_version_name']} maps to {expected}, "
            f"not AppVersion {current['app_version']}"
        )
    if not tag:
        if current["revision"] != 0:
            raise ReleaseVersionError(
                "untagged builds require AppReleaseRevision to be zero"
            )
        return source, {
            **current,
            "version_name": current["base_version_name"],
            "release_tag": "",
        }
    match = TAG_PATTERN.fullmatch(tag)
    if not match:
        raise ReleaseVersionError(
            "tag must match pre-release-v<base>-<revision> with revision 1..99"
        )
    base_version = match.group("base")
    revision = int(match.group("revision"))
    if base_version != current["base_version_name"]:
        raise ReleaseVersionError(
            f"tag base {base_version} does not match AppVersionStr "
            f"{current['base_version_name']}"
        )
    return source, {
        "app_version": current["app_version"],
        "base_version_name": base_version,
        "revision": revision,
        "version_name": f"{base_version}-{revision}",
        "release_tag": tag,
    }


def apply_revision(path, source, revision):
    updated, count = APP_RELEASE_REVISION_PATTERN.subn(
        f"constexpr auto AppReleaseRevision = {revision};",
        source,
        count=1,
    )
    if count != 1:
        raise ReleaseVersionError("could not update AppReleaseRevision")
    pathlib.Path(path).write_text(updated, encoding="utf-8")


def write_outputs(path, values):
    with pathlib.Path(path).open("a", encoding="utf-8") as output:
        for name in (
                "app_version",
                "base_version_name",
                "revision",
                "version_name",
                "release_tag"):
            output.write(f"{name}={values[name]}\n")


def parser():
    result = argparse.ArgumentParser()
    result.add_argument("--tag", default="")
    result.add_argument(
        "--version-header",
        default="Telegram/SourceFiles/core/version.h",
    )
    result.add_argument("--apply", action="store_true")
    result.add_argument("--github-output")
    return result


def main():
    args = parser().parse_args()
    try:
        source, values = resolve_version(args.tag, args.version_header)
        if args.apply and args.tag:
            apply_revision(args.version_header, source, values["revision"])
        if args.github_output:
            write_outputs(args.github_output, values)
        else:
            for name, value in values.items():
                print(f"{name}={value}")
    except (OSError, ReleaseVersionError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
