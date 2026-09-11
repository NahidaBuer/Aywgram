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


def load_contract(path: Path) -> dict[str, str]:
    return {
        key: json.loads(f'"{encoded}"')
        for key, encoded in STRING_PATTERN.findall(path.read_text(encoding="utf-8"))
    }


def parse_args() -> argparse.Namespace:
    repository = Path(__file__).resolve().parents[4]
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=repository)
    parser.add_argument("--write", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repository = args.repository.resolve()
    packs = repository / "Telegram/Resources/ayw_langpacks"
    source = packs / "source/en.json"
    expected = load_contract(repository / "Telegram/Resources/langs/lang.strings")
    current = json.loads(source.read_text(encoding="utf-8")) if source.is_file() else None
    zh_path = "translations/zh-hans.json"
    zh_source = repository / "Telegram/Resources/langs/zh-hans.lproj/zh-hans.json"
    expected_zh = json.loads(zh_source.read_text(encoding="utf-8"))
    current_zh = (
        json.loads((packs / zh_path).read_text(encoding="utf-8"))
        if (packs / zh_path).is_file() else None
    )
    if current == expected and current_zh == expected_zh:
        print(f"LanguagePacks English and Simplified Chinese are current ({len(expected)} keys).")
        return 0
    if not args.write:
        print("LanguagePacks English or Simplified Chinese is stale; rerun with --write.")
        return 1
    sync = [sys.executable, "scripts/sync_sources.py", "--desktop", str(repository)]
    subprocess.run(sync, cwd=packs, check=True)
    print("Review changed keys in every locale, then run build.py and the full i18n audit.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
