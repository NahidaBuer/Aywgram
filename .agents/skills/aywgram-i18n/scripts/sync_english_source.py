#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path

from audit_i18n import load_catalog, load_desktop_catalog, load_json


ENTRY_PATTERN = re.compile(r'^(\s*)"([^"\n]+)"\s*:\s*"(?:\\.|[^"\\])*"(,?)$')


def parse_args() -> argparse.Namespace:
    repository = Path(__file__).resolve().parents[4]
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=repository)
    parser.add_argument(
        "--languages",
        type=Path,
        default=repository.parent / "Languages",
    )
    parser.add_argument("--write", action="store_true")
    return parser.parse_args()


def replacement_line(line: str, key: str, value: str) -> str:
    match = ENTRY_PATTERN.match(line)
    if not match:
        raise ValueError(f"Could not replace JSON entry for {key}")
    indent, _, comma = match.groups()
    encoded = json.dumps(value, ensure_ascii=False)
    return f'{indent}"{key}": {encoded}{comma}'


def apply_updates(
    path: Path,
    updates: dict[str, str],
    additions: dict[str, str],
) -> str:
    content = path.read_text(encoding="utf-8")
    had_newline = content.endswith("\n")
    lines = content.splitlines()
    found: set[str] = set()

    for index, line in enumerate(lines):
        match = ENTRY_PATTERN.match(line)
        if not match:
            continue
        key = match.group(2)
        if key in updates:
            lines[index] = replacement_line(line, key, updates[key])
            found.add(key)

    missing_updates = updates.keys() - found
    if missing_updates:
        missing = ", ".join(sorted(missing_updates))
        raise ValueError(f"Could not locate entries to update: {missing}")

    closing = next(
        index
        for index in range(len(lines) - 1, -1, -1)
        if lines[index].strip() == "}"
    )
    previous = closing - 1
    while previous >= 0 and not lines[previous].strip():
        previous -= 1
    if additions and not lines[previous].rstrip().endswith(","):
        lines[previous] = f"{lines[previous]},"
    if additions and previous + 1 == closing:
        lines.insert(closing, "")
        closing += 1

    new_lines = []
    sorted_additions = sorted(additions.items())
    for index, (key, value) in enumerate(sorted_additions):
        comma = "," if index + 1 < len(sorted_additions) else ""
        encoded = json.dumps(value, ensure_ascii=False)
        new_lines.append(f'  "{key}": {encoded}{comma}')
    lines[closing:closing] = new_lines

    result = "\n".join(lines)
    if had_newline:
        result += "\n"
    return result


def main() -> int:
    args = parse_args()
    repository = args.repository.resolve()
    languages = args.languages.resolve()
    lang_strings = repository / "Telegram/Resources/langs/lang.strings"
    english_path = languages / "values/langs/en/Shared.json"

    catalog = load_catalog(lang_strings)
    english, duplicates, _ = load_desktop_catalog(english_path)
    raw_source, raw_duplicates = load_json(english_path)
    if duplicates or raw_duplicates:
        raise ValueError("Refusing to sync a source file with duplicate keys")

    drift = {
        key
        for key, value in catalog.items()
        if english.get(key) != value
    }
    updates: dict[str, str] = {}
    additions: dict[str, str] = {}
    for key in sorted(drift):
        override = f"{key}_PC"
        if override in raw_source:
            updates[override] = catalog[key]
        else:
            additions[override] = catalog[key]

    print(
        f"Desktop source drift: {len(drift)} "
        f"updates={len(updates)} additions={len(additions)}"
    )
    if not drift:
        return 0
    if not args.write:
        for key in sorted(drift):
            print(key)
        return 1

    content = apply_updates(english_path, updates, additions)
    json.loads(content)
    english_path.write_text(content, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
