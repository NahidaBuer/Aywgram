import argparse
import csv
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys

from out_layout import TARGETS


ROOT = Path(__file__).resolve().parents[2]
CATEGORIES = ('temporary', 'intermediates', 'binaries', 'packages', 'compiler-cache', 'downloads', 'evidence')


def linked(path):
    return path.is_symlink() or getattr(path, 'is_junction', lambda: False)()


def contained(root, relative):
    parts = Path(relative).parts
    if not parts or Path(relative).is_absolute() or '..' in parts or parts[0] != 'out':
        raise ValueError('Cleanup paths must be relative paths below out/')
    path = root
    for part in parts:
        path /= part
        if linked(path):
            raise ValueError(f'Refusing a symlink or junction in cleanup path: {relative}')
    if path == root / 'out' or not path.resolve().is_relative_to(root / 'out'):
        raise ValueError('Refusing cleanup of the out root or a path outside it')
    return path


def children(path):
    if not path.is_dir() or linked(path):
        return []
    return sorted((p for p in path.iterdir() if not linked(p)), key=lambda p: p.name)


def candidates(root):
    out = root / 'out'
    result = {}
    for family, category in (('build', 'intermediates'), ('artifacts', 'binaries')):
        for target in TARGETS:
            for configuration in ('Release', 'Debug'):
                for path in children(out / family / target / configuration):
                    if path.is_dir() and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', path.name):
                        result[str(path.relative_to(root))] = category
    for path in children(out / 'dependencies/build'):
        if path.is_dir() and path.name in TARGETS:
            result[str(path.relative_to(root))] = 'intermediates'
    for workflow in children(out / 'tmp'):
        for path in children(workflow):
            result[str(path.relative_to(root))] = 'temporary'
    for path in children(out / 'dist'):
        if path.is_file() and re.fullmatch(r'AywGram-v[0-9][0-9.-]*-(?:windows-(?:x86_64|arm64)|mac-(?:arm64|x86_64|universal)|linux-x86_64)\.(?:zip|tar\.gz)', path.name):
            result[str(path.relative_to(root))] = 'packages'
    for path in children(out / 'cache/compiler'):
        if path.is_dir() and path.name in TARGETS:
            result[str(path.relative_to(root))] = 'compiler-cache'
    for path in children(out / 'cache/downloads'):
        result[str(path.relative_to(root))] = 'downloads'
    for family in ('plans', 'metadata'):
        for path in children(out / family):
            result[str(path.relative_to(root))] = 'evidence'
    for run in children(out / 'upstream-sync'):
        for name, category in (('evidence', 'evidence'), ('tmp', 'temporary')):
            path = run / name
            if path.is_dir() and not linked(path):
                result[str(path.relative_to(root))] = category
    return result


def entries(path):
    yield path
    if path.is_dir() and not linked(path):
        for directory, dirs, files in os.walk(path, followlinks=False):
            for name in sorted(dirs + files):
                yield Path(directory) / name
            dirs[:] = [name for name in dirs if not linked(Path(directory) / name)]


def snapshot(path):
    digest = hashlib.sha256()
    size = 0
    logs = []
    for item in sorted(entries(path)):
        relative = '.' if item == path else item.relative_to(path).as_posix()
        if item.name == '.git':
            raise ValueError(f'Refusing a source checkout inside cleanup candidate: {path}')
        info = item.lstat()
        digest.update(json.dumps((relative, info.st_mode, info.st_size, info.st_mtime_ns, info.st_ino)).encode())
        if stat.S_ISREG(info.st_mode):
            size += info.st_size
            if item.suffix in ('.log', '.xcactivitylog') or item.name == 'CMakeConfigureLog.yaml':
                logs.append(item)
    return {'bytes': size, 'snapshot': digest.hexdigest()}, logs


def process_running(pid, windows=False):
    if windows:
        output = subprocess.check_output(
            ['tasklist', '/FI', f'PID eq {pid}', '/FO', 'CSV', '/NH'], text=True)
        return any(len(row) > 1 and row[1] == str(pid) for row in csv.reader(output.splitlines()))
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        pass
    return True


def ensure_idle(root):
    for marker in (root / 'out/logs/build').glob('**/*.active.json'):
        if linked(marker):
            raise ValueError('Unexpected linked build activity marker')
        pid = json.loads(marker.read_text(encoding='utf-8'))['pid']
        if type(pid) is not int or pid <= 0:
            raise ValueError('Invalid build activity marker')
        if not process_running(pid, windows=os.name == 'nt'):
            continue
        raise ValueError(f'A build command may still be active (PID {pid}); stop it before cleaning')


def scan(root, categories):
    if (root / 'out').is_symlink():
        raise ValueError('out/ must not be a symlink')
    result = []
    for relative, category in sorted(candidates(root).items()):
        if category not in categories:
            continue
        path = contained(root, relative)
        state, logs = snapshot(path)
        result.append({'path': relative, 'category': category, **state, 'preserved_logs': len(logs)})
    return {'schema': 1, 'root': str(root), 'categories': sorted(categories), 'items': result}


def apply(root, plan, report_dir):
    if plan.get('schema') != 1 or plan.get('root') != str(root):
        raise ValueError('Cleanup plan does not belong to this checkout')
    categories = set(plan.get('categories', []))
    if not categories or categories - set(CATEGORIES):
        raise ValueError('Select recognized cleanup categories')
    ensure_idle(root)
    if scan(root, categories) != plan:
        raise ValueError('Cleanup candidates changed since preview; generate a fresh plan')
    report_dir.mkdir(parents=True, exist_ok=True)
    report = {'removed': [], 'preserved_logs': [], 'error': None}
    try:
        for item in plan['items']:
            path = contained(root, item['path'])
            state, logs = snapshot(path)
            if state['snapshot'] != item['snapshot']:
                raise ValueError(f'Candidate changed during cleanup: {item["path"]}')
            for log in logs:
                relative = log.relative_to(root)
                destination = report_dir / 'preserved' / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(log, destination)
                if hashlib.sha256(log.read_bytes()).digest() != hashlib.sha256(destination.read_bytes()).digest():
                    raise ValueError(f'Log preservation verification failed: {log}')
                report['preserved_logs'].append(str(relative))
            if path.is_dir():
                shutil.rmtree(path)
            else:
                path.unlink()
            report['removed'].append(item)
    except (OSError, ValueError) as error:
        report['error'] = str(error)
        raise
    finally:
        (report_dir / 'report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        lines = ['# out cleanup', '', f'Removed candidates: {len(report["removed"])}',
            f'Preserved logs: {len(report["preserved_logs"])}', f'Error: {report["error"] or "none"}', '']
        lines += [f'- {item["category"]}: `{item["path"]}` ({item["bytes"]} bytes)' for item in report['removed']]
        (report_dir / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return report


def main():
    parser = argparse.ArgumentParser(description='Selectively clean only the documented out layout; preserve logs and final reports.')
    parser.add_argument('--root', type=Path, default=ROOT)
    sub = parser.add_subparsers(dest='command', required=True)
    preview = sub.add_parser('scan')
    preview.add_argument('--category', choices=CATEGORIES, action='append')
    preview.add_argument('--output', type=Path)
    execute = sub.add_parser('apply')
    execute.add_argument('--plan', type=Path, required=True)
    args = parser.parse_args()
    try:
        root = args.root.resolve()
        actual = subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--show-toplevel'], text=True).strip()
        if Path(actual).resolve() != root:
            raise ValueError('--root must identify the repository root')
        if args.command == 'scan':
            plan = scan(root, set(args.category or CATEGORIES))
            if args.output:
                destination = args.output.resolve()
                if not destination.is_relative_to(root / 'out/logs/cleanup'):
                    raise ValueError('Cleanup plans must be saved below out/logs/cleanup/')
                destination.parent.mkdir(parents=True, exist_ok=True)
                destination.write_text(json.dumps(plan, indent=2) + '\n', encoding='utf-8')
            print(json.dumps(plan, indent=2))
        else:
            stamp = datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S-%f')
            report_dir = contained(root, f'out/logs/cleanup/{stamp}')
            print(json.dumps(apply(root, json.loads(args.plan.read_text(encoding='utf-8')), report_dir), indent=2))
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        print(f'::error::{error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
