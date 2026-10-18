import argparse
import datetime
import json
from pathlib import Path
import re
import subprocess
import sys

from release_version import TAG_PATTERN, resolve_version


ROOT = Path(__file__).resolve().parents[2]
META = re.compile(r'^<!-- ayw-release (\{[^\n]+\}) -->$', re.MULTILINE)
SECTIONS = ('AywGram', 'Contributors / 贡献者', 'AyuGram', 'Telegram Desktop')
CI_START = '<!-- ayw-ci:start -->'
CI_END = '<!-- ayw-ci:end -->'
INTRO = (
    'AywGram changelog / AywGram 发行改动\n\n'
    '本文件仅记录 AywGram 自身改动，从采用新发布流程的下一版开始。\n'
    'Only AywGram changes are recorded here, starting with the next release.\n\n'
    'Telegram Desktop: https://github.com/telegramdesktop/tdesktop/blob/dev/changelog.txt\n'
    'AyuGram: https://github.com/AyuGram/AyuGramDesktop/releases\n'
)


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()


def read_at(root, path, ref=None):
    if ref:
        return subprocess.check_output(
            ['git', '-C', str(root), 'show', f'{ref}:{path}'], text=True, stderr=subprocess.PIPE)
    return (root / path).read_text(encoding='utf-8')


def parse_document(text, tag):
    match = TAG_PATTERN.fullmatch(tag)
    if not match:
        raise ValueError('Invalid revisioned release tag')
    found = list(META.finditer(text))
    if len(found) != 1:
        raise ValueError('Expected one ayw-release JSON metadata comment')
    metadata = json.loads(found[0][1])
    if metadata.get('tag') != tag:
        raise ValueError('Release notes tag does not match requested tag')
    if not TAG_PATTERN.fullmatch(metadata.get('previous_tag', '')):
        raise ValueError('previous_tag must identify an Ayw distribution release')
    if metadata['previous_tag'] == tag:
        raise ValueError('previous_tag must differ from the release tag')
    if not re.fullmatch(r'[0-9a-f]{40}', metadata.get('reviewed_commit', '')):
        raise ValueError('reviewed_commit must be a complete commit SHA')
    datetime.date.fromisoformat(metadata['date'])
    if set(metadata.get('upstreams', {})) != {'ayu', 'telegram'}:
        raise ValueError('Record both ayu and telegram upstream baselines')
    for source in metadata['upstreams'].values():
        if not isinstance(source, dict) or not all(
                re.fullmatch(r'[0-9a-f]{40}', source.get(key, ''))
                for key in ('from', 'to')):
            raise ValueError('Upstream baselines must contain complete from/to SHAs')
        if not source.get('url', '').startswith('https://github.com/'):
            raise ValueError('Upstream evidence requires a GitHub source URL')
    headings = list(re.finditer(r'^## (.+)$', text, re.MULTILINE))
    if tuple(h[1] for h in headings) != SECTIONS:
        raise ValueError('Release sections must be AywGram, contributors, AyuGram, Telegram Desktop')
    sections = {}
    for index, heading in enumerate(headings):
        end = headings[index + 1].start() if index + 1 < len(headings) else len(text)
        body = text[heading.end():end].strip()
        if not body or re.search(r'\b(?:TODO|TBD|PLACEHOLDER)\b', body):
            raise ValueError(f'Incomplete release section: {heading[1]}')
        if heading[1] != SECTIONS[1] and not all(
                re.search(r'^### ' + language + r'\s*\n\s*\S', body, re.MULTILINE)
                for language in ('简体中文', 'English')):
            raise ValueError(f'Both language sections are required: {heading[1]}')
        sections[heading[1]] = body
    return metadata, sections


def document_path(tag):
    if not TAG_PATTERN.fullmatch(tag):
        raise ValueError('Invalid revisioned release tag')
    return f'docs/releases/{tag.removeprefix("pre-release-v")}.md'


def load_document(root, tag, ref=None):
    text = read_at(root, document_path(tag), ref)
    metadata, sections = parse_document(text, tag)
    return text, metadata, sections


def changelog(root, ref=None):
    if ref:
        paths = git(root, 'ls-tree', '-r', '--name-only', ref, '--', 'docs/releases').splitlines()
    else:
        paths = [str(p.relative_to(root)) for p in (root / 'docs/releases').glob('*.md')]
    releases = []
    for path in paths:
        version = Path(path).stem
        tag = 'pre-release-v' + version
        match = TAG_PATTERN.fullmatch(tag)
        if not match:
            continue
        _, metadata, sections = load_document(root, tag, ref)
        numbers = tuple(int(part) for part in match['base'].split('.'))
        numbers = (*numbers, 0) if len(numbers) == 2 else numbers
        releases.append((metadata['date'], *numbers, int(match['revision']), version, sections))
    result = INTRO + '\n'
    for date, *_, version, sections in sorted(releases, reverse=True):
        formatted = datetime.date.fromisoformat(date).strftime('%d.%m.%y')
        result += f'{version} ({formatted})\n\n'
        for name in SECTIONS[:2]:
            result += name + '\n\n'
            for line in sections[name].splitlines():
                line = re.sub(r'^### ', '', line)
                line = re.sub(r'\[([^\]]+)\]\((https://[^\s)]+)\)', r'\1 (\2)', line)
                result += line + '\n'
            result += '\n'
    return result.rstrip() + '\n'


def validate(root, tag, source_ref='HEAD'):
    _, metadata, _ = load_document(root, tag, source_ref)
    header = root / 'Telegram/SourceFiles/core/version.h'
    source_header = read_at(root, str(header.relative_to(root)), source_ref)
    import tempfile
    scratch = root / 'out/tmp/release-validation'
    scratch.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=scratch) as temp:
        path = Path(temp) / 'version.h'
        path.write_text(source_header, encoding='utf-8')
        _, version = resolve_version(tag, path)
        if version['revision'] == 0:
            raise ValueError('Release revision must be nonzero')
        if not re.search(r'constexpr auto AppReleaseRevision = 0;', source_header):
            raise ValueError('Committed source must retain revision zero')
    reviewed = metadata['reviewed_commit']
    previous = git(root, 'rev-parse', '--verify', metadata['previous_tag'] + '^{commit}')
    for earlier, later in ((previous, reviewed), (reviewed, source_ref)):
        subprocess.run(['git', '-C', str(root), 'merge-base', '--is-ancestor', earlier, later], check=True)
    changes = set(git(root, 'diff', '--name-only', reviewed, source_ref).splitlines())
    if changes - {document_path(tag), 'changelog.txt'}:
        raise ValueError('Substantive changes after reviewed_commit require another release review')
    if read_at(root, 'changelog.txt', source_ref) != changelog(root, source_ref):
        raise ValueError('Root changelog is stale; run release_notes.py changelog')


def compose(editorial, ci, existing=''):
    if CI_START in editorial or CI_END in editorial or CI_START in ci or CI_END in ci:
        raise ValueError('Input contains reserved CI markers')
    if existing:
        if existing.count(CI_START) != 1 or existing.count(CI_END) != 1:
            raise ValueError('Existing release body must contain exactly one CI block')
        start, end = existing.index(CI_START), existing.index(CI_END)
        if end < start:
            raise ValueError('CI markers are reversed')
        prefix, suffix = existing[:start], existing[end + len(CI_END):]
    else:
        prefix, suffix = editorial.rstrip() + '\n\n', '\n'
    return prefix + CI_START + '\n' + ci.strip() + '\n' + CI_END + suffix


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--root', type=Path, default=ROOT)
    sub = parser.add_subparsers(dest='command', required=True)
    check = sub.add_parser('validate')
    check.add_argument('--tag', required=True)
    check.add_argument('--source-ref', default='HEAD')
    render = sub.add_parser('changelog')
    render.add_argument('--check', action='store_true')
    merge = sub.add_parser('compose')
    merge.add_argument('--tag', required=True)
    merge.add_argument('--ci-body', type=Path, required=True)
    merge.add_argument('--existing-json', type=Path)
    merge.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        if args.command == 'validate':
            validate(root, args.tag, args.source_ref)
        elif args.command == 'changelog':
            generated = changelog(root)
            if args.check:
                if (root / 'changelog.txt').read_text(encoding='utf-8') != generated:
                    raise ValueError('Root changelog differs from release documents')
            else:
                (root / 'changelog.txt').write_text(generated, encoding='utf-8')
        else:
            editorial, _, _ = load_document(root, args.tag)
            editorial = META.sub('', editorial).strip()
            existing = json.loads(args.existing_json.read_text(encoding='utf-8')).get('body', '') if args.existing_json else ''
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_text(compose(editorial, args.ci_body.read_text(encoding='utf-8'), existing), encoding='utf-8')
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f'::error::{error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
