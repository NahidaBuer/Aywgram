#!/usr/bin/env python3

import argparse
import json
import re
from pathlib import Path


PLURAL_SUFFIXES = ("zero", "one", "two", "few", "many", "other")
STRING_PATTERN = re.compile(
    r'^"ayu_([^"\n]+)"\s*=\s*"((?:\\.|[^"\\])*)";$',
    re.MULTILINE,
)
PLACEHOLDER_PATTERN = re.compile(r"\{[a-zA-Z][a-zA-Z0-9_]*\}")


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


def load_catalog(path: Path) -> dict[str, str]:
    content = path.read_text(encoding="utf-8")
    return {
        key: json.loads(f'"{encoded}"')
        for key, encoded in STRING_PATTERN.findall(content)
    }


def desktop_key(key: str) -> str | None:
    if key.endswith("_Android"):
        return None
    for suffix in PLURAL_SUFFIXES:
        marker = f"_{suffix}"
        if key.endswith(marker):
            key = f"{key[:-len(marker)]}#{suffix}"
            break
    if key.endswith("_PC"):
        key = key[:-3]
    return key


def desktop_value(value: str) -> str:
    value = value.replace("&amp;", "&")
    if "%1$d" in value and "%2$d" not in value:
        return value.replace("%1$d", "{count}")
    if "%1$d" in value and "%2$d" in value:
        return value.replace("%1$d", "{count1}").replace("%2$d", "{count2}")
    if "%1$s" in value and "%2$s" not in value:
        return value.replace("%1$s", "{item}")
    if "%1$s" in value and "%2$s" in value:
        return value.replace("%1$s", "{item1}").replace("%2$s", "{item2}")
    return value


def load_desktop_catalog(
    path: Path,
) -> tuple[dict[str, str], set[str], set[str]]:
    data, duplicates = load_json(path)
    result: dict[str, str] = {}
    collisions: set[str] = set()
    for raw_key in sorted(data):
        key = desktop_key(raw_key)
        if key is None:
            continue
        value = data[raw_key]
        if not isinstance(value, str):
            raise ValueError(f"Expected a string value for {raw_key} in {path}")
        if key in result:
            collisions.add(key)
        result[key] = desktop_value(value)
    return result, duplicates, collisions


def generated_desktop_duplicates(path: Path) -> set[str]:
    data, _ = load_json(path)
    generated: set[str] = set()
    duplicates: set[str] = set()
    for raw_key in data:
        if raw_key.endswith("_Android") or f"{raw_key}_PC" in data:
            continue
        key = raw_key[:-3] if raw_key.endswith("_PC") else raw_key
        if any(f"_{suffix}" in key for suffix in ("zero", "two", "few", "many")):
            continue
        if key.endswith("_one"):
            key = f"{key[:-4]}#one"
        elif key.endswith("_other"):
            key = f"{key[:-6]}#other"
        if key in generated:
            duplicates.add(key)
        generated.add(key)
    return duplicates


def print_keys(label: str, keys: set[str]) -> None:
    if not keys:
        return
    print(f"  {label}:")
    for key in sorted(keys):
        print(f"    {key}")


def placeholders(value: str) -> set[str]:
    return set(PLACEHOLDER_PATTERN.findall(value))


def placeholder_mismatches(
    expected: dict[str, str],
    actual: dict[str, str],
) -> set[str]:
    return {
        key
        for key in expected.keys() & actual.keys()
        if placeholders(expected[key]) != placeholders(actual[key])
    }


def parse_args() -> argparse.Namespace:
    repository = Path(__file__).resolve().parents[4]
    parser = argparse.ArgumentParser()
    parser.add_argument("--repository", type=Path, default=repository)
    parser.add_argument(
        "--languages",
        type=Path,
        default=repository.parent / "Languages",
    )
    parser.add_argument("--details", action="store_true")
    parser.add_argument("--strict", action="store_true")
    parser.add_argument("--strict-translations", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    repository = args.repository.resolve()
    languages = args.languages.resolve()
    lang_strings = repository / "Telegram/Resources/langs/lang.strings"
    zh_hans_path = (
        repository / "Telegram/Resources/langs/zh-hans.lproj/zh-hans.json"
    )
    english_path = languages / "values/langs/en/Shared.json"
    language_files = sorted((languages / "values/langs").glob("*/Shared.json"))

    catalog = load_catalog(lang_strings)
    zh_hans, zh_duplicates, zh_collisions = load_desktop_catalog(zh_hans_path)
    english, en_duplicates, en_collisions = load_desktop_catalog(english_path)
    generated_duplicates = generated_desktop_duplicates(english_path)

    zh_missing = catalog.keys() - zh_hans.keys()
    zh_extra = zh_hans.keys() - catalog.keys()
    en_missing = catalog.keys() - english.keys()
    en_extra = english.keys() - catalog.keys()
    en_stale = {
        key
        for key in catalog.keys() & english.keys()
        if catalog[key] != english[key]
    }

    print("Blocking catalogs")
    print(
        f"  bundled zh-hans: expected={len(catalog)} actual={len(zh_hans)} "
        f"missing={len(zh_missing)} extra={len(zh_extra)} "
        f"duplicates={len(zh_duplicates)} overrides={len(zh_collisions)}"
    )
    print(
        f"  Languages/en: expected={len(catalog)} actual={len(english)} "
        f"missing={len(en_missing)} extra={len(en_extra)} "
        f"stale={len(en_stale)} duplicates={len(en_duplicates)} "
        f"overrides={len(en_collisions)} "
        f"generated-duplicates={len(generated_duplicates)}"
    )

    if args.details:
        print_keys("zh-hans missing", set(zh_missing))
        print_keys("zh-hans extra", set(zh_extra))
        print_keys("zh-hans duplicate raw keys", zh_duplicates)
        print_keys("zh-hans effective overrides", zh_collisions)
        print_keys("Languages/en missing", set(en_missing))
        print_keys("Languages/en shared or legacy extras", set(en_extra))
        print_keys("Languages/en stale Desktop values", en_stale)
        print_keys("Languages/en duplicate raw keys", en_duplicates)
        print_keys("Languages/en effective overrides", en_collisions)
        print_keys("Languages/en generated duplicates", generated_duplicates)

    print("Allowed translation lag")
    translation_missing: list[set[str]] = []
    translation_placeholder_errors: set[str] = set()
    translation_duplicates: set[str] = set()
    translatable_english = {
        key: value for key, value in english.items() if key in catalog
    }
    for path in language_files:
        if path == english_path:
            continue
        locale = path.parent.name
        translated, duplicates, collisions = load_desktop_catalog(path)
        recognized = {
            key: value for key, value in translated.items() if key in catalog
        }
        missing = translatable_english.keys() - recognized.keys()
        extra = translated.keys() - translatable_english.keys()
        placeholder_errors = placeholder_mismatches(
            translatable_english,
            recognized,
        )
        translation_missing.append(set(missing))
        translation_placeholder_errors.update(
            f"{locale}:{key}" for key in placeholder_errors
        )
        translation_duplicates.update(f"{locale}:{key}" for key in duplicates)
        print(
            f"  {locale}: expected={len(translatable_english)} "
            f"actual={len(recognized)} "
            f"missing={len(missing)} extra={len(extra)} "
            f"placeholder-errors={len(placeholder_errors)} "
            f"duplicates={len(duplicates)} overrides={len(collisions)}"
        )
        if args.details:
            print_keys(f"{locale} placeholder errors", placeholder_errors)
            print_keys(f"{locale} duplicate raw keys", duplicates)

    if translation_missing:
        common_missing = set.intersection(*translation_missing)
        print(f"  common missing in all translations: {len(common_missing)}")
        if args.details:
            print_keys("common translation lag", common_missing)

    blocking = any((
        zh_missing,
        zh_extra,
        zh_duplicates,
        en_missing,
        en_stale,
        en_duplicates,
        generated_duplicates,
        translation_placeholder_errors,
        translation_duplicates,
    ))
    translation_lag = any(translation_missing)
    if args.strict_translations and translation_lag:
        return 1
    if args.strict and blocking:
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
