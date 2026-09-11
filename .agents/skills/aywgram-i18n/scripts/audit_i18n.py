#!/usr/bin/env python3

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path


STRING_PATTERN = re.compile(
    r'^"ayu_([^"\n]+)"\s*=\s*"((?:\\.|[^"\\])*)";$',
    re.MULTILINE,
)
PLACEHOLDER_PATTERN = re.compile(r"\{[a-zA-Z][a-zA-Z0-9_]*\}")
REQUIRED_LINKS = {
    "LinksChannel": ("Channel", "频道"),
    "LinksGroup": ("Group Chat", "群聊"),
    "LinksRepository": ("Repository", "代码仓库"),
    "LinksIssues": ("Issue Tracker", "问题反馈"),
}
RETIRED_LINKS = {"LinksChats", "LinksTranslate", "LinksDocumentation"}
LINK_SOURCE_SNIPPETS = {
    'tr::ayu_LinksChannel()',
    'st::menuIconChannel',
    'u"@AywGram"_q',
    'u"https://t.me/AywGram"_q',
    'tr::ayu_LinksGroup()',
    'st::menuIconChats',
    'u"@AywGram_Group"_q',
    'u"https://t.me/AywGram_Group"_q',
    'tr::ayu_LinksRepository()',
    'st::menuIconLink',
    'u"GitHub"_q',
    'u"https://github.com/NahidaBuer/AywGram"_q',
    'tr::ayu_LinksIssues()',
    'st::menuIconReport',
    'u"GitHub Issues"_q',
    'u"https://github.com/NahidaBuer/AywGram/issues"_q',
}


def load_json(path: Path) -> tuple[dict[str, object], set[str]]:
    duplicates: set[str] = set()

    def collect(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                duplicates.add(key)
            result[key] = value
        return result

    with path.open(encoding="utf-8") as stream:
        data = json.load(stream, object_pairs_hook=collect)
    if not isinstance(data, dict):
        raise ValueError(f"Expected a JSON object in {path}")
    return data, duplicates


def parse_catalog(content: str) -> dict[str, str]:
    return {
        key: json.loads(f'"{encoded}"')
        for key, encoded in STRING_PATTERN.findall(content)
    }


def load_catalog(path: Path) -> dict[str, str]:
    return parse_catalog(path.read_text(encoding="utf-8"))


def git_file(repository: Path, ref: str, path: str) -> str:
    return subprocess.run(
        ["git", "show", f"{ref}:{path}"],
        cwd=repository,
        check=True,
        capture_output=True,
        encoding="utf-8",
    ).stdout


def placeholders(value: str) -> set[str]:
    return set(PLACEHOLDER_PATTERN.findall(value))


def print_keys(label: str, keys: set[str]) -> None:
    if keys:
        print(f"  {label}: {', '.join(sorted(keys))}")


def parse_args() -> argparse.Namespace:
    repository = Path(__file__).resolve().parents[4]
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=repository)
    parser.add_argument("--details", action="store_true")
    parser.add_argument(
        "--base-ref",
        default="HEAD",
        help="Desktop commit before the feature; require its changed keys in every locale",
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="compatibility alias; LanguagePacks validation now always runs",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repository = args.repository.resolve()
    catalog = load_catalog(repository / "Telegram/Resources/langs/lang.strings")
    zh_hans_raw, duplicates = load_json(
        repository / "Telegram/Resources/langs/zh-hans.lproj/zh-hans.json"
    )
    non_strings = {key for key, value in zh_hans_raw.items() if not isinstance(value, str)}
    zh_hans = {
        key: value for key, value in zh_hans_raw.items() if isinstance(value, str)
    }
    missing = set(catalog) - set(zh_hans)
    extra = set(zh_hans) - set(catalog)
    empty = {key for key, value in zh_hans.items() if not value.strip()}
    placeholder_errors = {
        key
        for key in catalog.keys() & zh_hans.keys()
        if placeholders(catalog[key]) != placeholders(zh_hans[key])
    }
    style_errors = {
        key
        for key, value in zh_hans.items()
        if "你" in value
        or any(word in value for word in ("帐号", "账号", "帐户"))
        or "?" in value
    }
    link_errors = {
        key
        for key, expected in REQUIRED_LINKS.items()
        if catalog.get(key) != expected[0] or zh_hans.get(key) != expected[1]
    }
    retired_links = RETIRED_LINKS & (catalog.keys() | zh_hans.keys())
    settings_source = (
        repository / "Telegram/SourceFiles/ayu/ui/settings/settings_main.cpp"
    ).read_text(encoding="utf-8")
    missing_link_rows = {
        snippet for snippet in LINK_SOURCE_SNIPPETS if snippet not in settings_source
    }
    language_source = (
        repository / "Telegram/SourceFiles/ayu/ayu_lang.cpp"
    ).read_text(encoding="utf-8")
    runtime_errors: set[str] = set()
    if "tdata/ayu/languages/v1/" not in language_source:
        runtime_errors.add("versioned language cache")
    if "AywGram/LanguagePacks@main/dist/manifest.json" not in language_source:
        runtime_errors.add("LanguagePacks manifest URL")
    if 'tdata/ayu/languages/"_q' in language_source:
        runtime_errors.add("legacy cache is still read")

    print(
        "Desktop contracts: "
        f"english={len(catalog)} zh-hans={len(zh_hans)} "
        f"missing={len(missing)} extra={len(extra)} "
        f"placeholder-errors={len(placeholder_errors)} style-errors={len(style_errors)}"
    )
    if args.details:
        print_keys("missing", missing)
        print_keys("extra", extra)
        print_keys("duplicates", duplicates)
        print_keys("non-string", non_strings)
        print_keys("empty", empty)
        print_keys("placeholder errors", placeholder_errors)
        print_keys("Simplified Chinese style errors", style_errors)
        print_keys("Links contract errors", link_errors)
        print_keys("retired Links keys", retired_links)
        print_keys("missing Links rows", missing_link_rows)
        print_keys("runtime contract errors", runtime_errors)

    failed = any((
        missing,
        extra,
        duplicates,
        non_strings,
        empty,
        placeholder_errors,
        style_errors,
        link_errors,
        retired_links,
        missing_link_rows,
        runtime_errors,
    ))
    packs = repository / "Telegram/Resources/ayw_langpacks"
    if not (packs / "scripts/audit.py").is_file():
        print("LanguagePacks submodule is not initialized.")
        return 1
    pack_source, source_duplicates = load_json(packs / "source/en.json")
    pack_zh_hans, zh_duplicates = load_json(packs / "translations/zh-hans.json")
    if pack_source != catalog or source_duplicates:
        print("LanguagePacks source/en.json is not synchronized or has duplicate keys.")
        failed = True
    if pack_zh_hans != zh_hans or zh_duplicates:
        print("LanguagePacks translations/zh-hans.json is not synchronized or has duplicate keys.")
        failed = True
    try:
        desktop_base = parse_catalog(git_file(
            repository, args.base_ref, "Telegram/Resources/langs/lang.strings"
        ))
        pack_base = json.loads(git_file(packs, "HEAD", "source/en.json"))
    except (subprocess.CalledProcessError, json.JSONDecodeError) as error:
        print(f"Could not load localization baseline: {error}")
        return 1
    changed_keys = {
        key for key, value in catalog.items()
        if desktop_base.get(key) != value or pack_base.get(key) != value
    }
    print(f"Feature translation contract: changed-keys={len(changed_keys)}")
    if args.details:
        print_keys("required in every locale", changed_keys)
    sys.stdout.flush()
    command = [sys.executable, "scripts/audit.py"]
    for key in sorted(changed_keys):
        command.extend(["--required-key", key])
    if subprocess.run(command, cwd=packs, check=False).returncode:
        failed = True
    return int(failed)


if __name__ == "__main__":
    raise SystemExit(main())
