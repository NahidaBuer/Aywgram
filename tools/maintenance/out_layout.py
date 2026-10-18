import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
TARGETS = ('windows-x86_64', 'windows-arm64', 'mac-arm64', 'mac-x86_64', 'mac-universal', 'linux-x86_64')


def component(value):
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]*', value):
        raise ValueError('Output path component must be a simple name')
    return value


def paths(root, target, configuration, profile, run_id):
    if target not in TARGETS or configuration not in ('Release', 'Debug'):
        raise ValueError('Unsupported target or configuration')
    component(profile)
    component(run_id)
    return {
        'AYW_BUILD_DIR': root / 'out/build' / target / configuration / profile,
        'AYW_ARTIFACT_DIR': root / 'out/artifacts' / target / configuration / run_id,
        'AYW_DIST_DIR': root / 'out/dist',
        'AYW_METADATA_DIR': root / 'out/metadata' / run_id,
        'AYW_LOG_DIR': root / 'out/logs/build' / run_id,
    }


def stage(source, destination, target):
    if destination.exists() and any(destination.iterdir()):
        raise ValueError('Artifact directory is not empty; select a new run-id')
    required = (('AywGram.exe', 'Updater.exe') if target.startswith('windows') else
        ('AywGram.app',) if target.startswith('mac-') else ('AywGram', 'Updater'))
    for name in required:
        if not (source / name).exists():
            raise ValueError(f'Missing native build product: {source / name}')
    destination.mkdir(parents=True, exist_ok=True)
    names = list(required)
    if target.startswith('windows') and (source / 'modules').is_dir():
        names.append('modules')
    for name in names:
        src, dst = source / name, destination / name
        if src.is_dir():
            if sys.platform == 'darwin' and name.endswith('.app'):
                subprocess.run(['ditto', str(src), str(dst)], check=True)
            else:
                shutil.copytree(src, dst, symlinks=True)
        else:
            shutil.copy2(src, dst)


def run_logged(log, command):
    if not command:
        raise ValueError('A command is required after --')
    log.parent.mkdir(parents=True, exist_ok=True)
    active = log.parent / (log.name + '.active.json')
    if active.is_symlink():
        raise ValueError('Build activity marker must not be a symlink')
    with log.open('ab') as output:
        child = subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        active.write_text(json.dumps({'pid': child.pid, 'log': str(log)}), encoding='utf-8')
        try:
            for chunk in iter(lambda: child.stdout.read1(65536), b''):
                output.write(chunk)
                output.flush()
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
            return child.wait()
        finally:
            if child.poll() is None:
                child.terminate()
                child.wait()
            child.stdout.close()
            active.unlink(missing_ok=True)


def report(log_dir, status, version, asset=None):
    layout = log_dir / 'layout.json'
    result = json.loads(layout.read_text(encoding='utf-8')) if layout.exists() else {}
    result.update(status=status, version_name=version, package=None)
    if asset and asset.is_file():
        digest = hashlib.sha256()
        with asset.open('rb') as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                digest.update(chunk)
        result['package'] = {'path': str(asset), 'bytes': asset.stat().st_size, 'sha256': digest.hexdigest()}
    log_dir.mkdir(parents=True, exist_ok=True)
    (log_dir / 'report.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    lines = ['# Build report', '', f'Status: {status}', f'Version: {version}',
        f'Source: {result.get("source_sha", "unavailable")}', '',
        'Application runtime and UI behavior were not verified by this report.', '']
    if result['package']:
        lines.extend(f'{key}: {value}' for key, value in result['package'].items())
    (log_dir / 'report.md').write_text('\n'.join(lines) + '\n', encoding='utf-8')
    return result


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='command', required=True)
    initialize = sub.add_parser('paths')
    initialize.add_argument('--target', choices=TARGETS, required=True)
    initialize.add_argument('--configuration', choices=('Release', 'Debug'), default='Release')
    initialize.add_argument('--profile', default='default')
    initialize.add_argument('--run-id', default=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%d-%H%M%S-build'))
    initialize.add_argument('--github-env', type=Path)
    copy = sub.add_parser('stage')
    copy.add_argument('--target', choices=TARGETS, required=True)
    copy.add_argument('--source', type=Path, required=True)
    copy.add_argument('--destination', type=Path, required=True)
    logged = sub.add_parser('run')
    logged.add_argument('--log', type=Path, required=True)
    logged.add_argument('args', nargs=argparse.REMAINDER)
    final = sub.add_parser('report')
    final.add_argument('--log-dir', type=Path, required=True)
    final.add_argument('--status', required=True)
    final.add_argument('--version', default='')
    final.add_argument('--asset', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'paths':
            values = paths(ROOT, args.target, args.configuration, args.profile, args.run_id)
            for path in values.values():
                if path.resolve() != path or not path.resolve().is_relative_to(ROOT / 'out'):
                    raise ValueError('Output path must stay inside repository out/')
            values['AYW_LOG_DIR'].mkdir(parents=True, exist_ok=True)
            if args.github_env:
                with args.github_env.open('a', encoding='utf-8') as stream:
                    for key, value in values.items():
                        stream.write(f'{key}={value.as_posix()}\n')
            report = {name: path.as_posix() for name, path in values.items()}
            report.update(target=args.target, configuration=args.configuration, run_id=args.run_id)
            report['source_sha'] = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
            (values['AYW_LOG_DIR'] / 'layout.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
            print(json.dumps(report, indent=2))
        elif args.command == 'stage':
            if not args.destination.resolve().is_relative_to(ROOT / 'out/artifacts'):
                raise ValueError('Staged artifacts must be inside out/artifacts/')
            stage(args.source, args.destination, args.target)
        elif args.command == 'report':
            if not args.log_dir.resolve().is_relative_to(ROOT / 'out/logs'):
                raise ValueError('Final reports must stay inside out/logs/')
            report(args.log_dir, args.status, args.version, args.asset)
        else:
            if not args.log.resolve().is_relative_to(ROOT / 'out/logs'):
                raise ValueError('Command logs must be inside out/logs/')
            return run_logged(args.log, args.args[1:] if args.args[:1] == ['--'] else args.args)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f'::error::{error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
