import json
import hashlib
import io
import os
from pathlib import Path
import tempfile
import unittest
import sys
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import clean_out as clean
import out_layout


class CleanOutTests(unittest.TestCase):
    def setUp(self):
        scratch = clean.ROOT / 'out/tmp/tests'
        scratch.mkdir(parents=True, exist_ok=True)
        directory = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def file(self, name, data='content'):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(data)
        return path

    def apply(self, plan):
        return clean.apply(self.root, plan, self.root / 'out/logs/cleanup/result')

    def test_selected_intermediates_keep_program_packages_and_logs(self):
        tree = 'out/build/mac-arm64/Release/default/'
        obj = self.file(tree + 'objects/file.o')
        native_log = self.file(tree + 'CMakeFiles/CMakeConfigureLog.yaml', 'diagnostic')
        app = self.file('out/artifacts/mac-arm64/Release/run/AywGram.app/executable')
        archive = self.file('out/dist/AywGram-v7.3.1-2-mac-arm64.zip')
        final = self.file('out/logs/build/run/report.md')
        legacy = self.file('out/build/Telegram.build/object.o')
        plan = clean.scan(self.root, {'intermediates'})
        result = self.apply(plan)
        self.assertFalse(obj.exists())
        self.assertFalse(native_log.exists())
        for path in (app, archive, final, legacy):
            self.assertTrue(path.exists())
        self.assertEqual(len(result['preserved_logs']), 1)
        copy = self.root / 'out/logs/cleanup/result/preserved' / native_log.relative_to(self.root)
        self.assertEqual(copy.read_text(), 'diagnostic')

    def test_raw_evidence_is_optional_and_final_report_survives(self):
        evidence = self.file('out/upstream-sync/run/evidence/audit.json')
        report = self.file('out/upstream-sync/run/report.md')
        log = self.file('out/upstream-sync/run/logs/merge.log')
        temporary = self.file('out/upstream-sync/run/tmp/download')
        self.apply(clean.scan(self.root, {'temporary'}))
        self.assertFalse(temporary.exists())
        self.assertTrue(evidence.exists())
        self.apply(clean.scan(self.root, {'evidence'}))
        self.assertFalse(evidence.exists())
        self.assertTrue(report.exists())
        self.assertTrue(log.exists())

    def test_changed_inventory_stops_before_deletion(self):
        path = self.file('out/tmp/tests/run/scratch')
        plan = clean.scan(self.root, {'temporary'})
        path.write_text('changed')
        with self.assertRaisesRegex(ValueError, 'changed'):
            self.apply(plan)
        self.assertTrue(path.exists())

    def test_symlink_target_and_unknown_layout_are_untouched(self):
        target = self.file('outside/important.txt')
        work = self.file('out/tmp/tests/run/scratch')
        (work.parent / 'external').symlink_to(target.parent, target_is_directory=True)
        self.apply(clean.scan(self.root, {'temporary'}))
        self.assertTrue(target.exists())
        linked_root = self.root / 'out/build/mac-arm64/Release/default'
        linked_root.parent.mkdir(parents=True)
        linked_root.symlink_to(target.parent, target_is_directory=True)
        self.assertEqual(clean.scan(self.root, {'intermediates'})['items'], [])

    def test_checkout_and_tampered_plans_cannot_be_deleted(self):
        source = self.file('out/tmp/tests/source/.git/config')
        with self.assertRaisesRegex(ValueError, 'source checkout'):
            clean.scan(self.root, {'temporary'})
        plan = clean.scan(self.root, {'packages'})
        plan['items'] = [{'path': 'out/logs/build'}]
        with self.assertRaises(ValueError):
            self.apply(plan)
        self.assertTrue(source.exists())
        for path in ('out', '../out', 'out/../important', '/out/tmp'):
            with self.subTest(path=path), self.assertRaises(ValueError):
                clean.contained(self.root, path)

    def test_live_activity_marker_stops_cleanup(self):
        path = self.file('out/tmp/tests/run/scratch')
        self.file('out/logs/build/run/build.log.active.json', json.dumps({'pid': os.getpid()}))
        with self.assertRaisesRegex(ValueError, 'active'):
            self.apply(clean.scan(self.root, {'temporary'}))
        self.assertTrue(path.exists())

    def test_windows_activity_check_only_queries_processes(self):
        with patch.object(clean.subprocess, 'check_output', return_value='"ninja.exe","123","Console","1","128 K"\n') as query:
            self.assertTrue(clean.process_running(123, windows=True))
            query.assert_called_with(['tasklist', '/FI', 'PID eq 123', '/FO', 'CSV', '/NH'], text=True)
        with patch.object(clean.subprocess, 'check_output', return_value='INFO: No tasks are running\n'):
            self.assertFalse(clean.process_running(123, windows=True))

    def test_staging_copies_complete_program_before_tree_cleanup(self):
        source = self.root / 'out/build/linux-x86_64/Release/default/Release'
        self.file(str(source.relative_to(self.root) / 'AywGram'))
        self.file(str(source.relative_to(self.root) / 'Updater'))
        destination = self.root / 'out/artifacts/linux-x86_64/Release/run'
        out_layout.stage(source, destination, 'linux-x86_64')
        self.apply(clean.scan(self.root, {'intermediates'}))
        self.assertTrue((destination / 'AywGram').exists())
        self.assertTrue((destination / 'Updater').exists())
        with self.assertRaisesRegex(ValueError, 'not empty'):
            out_layout.stage(source, destination, 'linux-x86_64')

    def test_logged_failures_keep_diagnostics_and_clear_activity(self):
        log = self.root / 'out/logs/build/run/build.log'
        captured = io.TextIOWrapper(io.BytesIO())
        self.addCleanup(captured.close)
        with patch.object(out_layout.sys, 'stdout', captured):
            result = out_layout.run_logged(log, [sys.executable, '-c',
                'import sys; print("compiler diagnostic"); sys.exit(7)'])
        self.assertEqual(result, 7)
        self.assertIn('compiler diagnostic', log.read_text())
        self.assertFalse(log.with_name('build.log.active.json').exists())

    def test_final_report_records_actual_package_hash(self):
        archive = self.file('out/dist/AywGram-v7.3.1-2-linux-x86_64.tar.gz', 'package bytes')
        result = out_layout.report(self.root / 'out/logs/build/run', 'success', '7.3.1-2', archive)
        self.assertEqual(result['package']['sha256'], hashlib.sha256(archive.read_bytes()).hexdigest())
        self.assertEqual(result['package']['bytes'], archive.stat().st_size)


if __name__ == '__main__':
    unittest.main()
