import datetime
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

import release_notes as notes


class ReleaseNotesTests(unittest.TestCase):
    def setUp(self):
        scratch = notes.ROOT / 'out/tmp/tests'
        scratch.mkdir(parents=True, exist_ok=True)
        directory = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.git('init', '-q')
        self.git('config', 'user.name', 'Test')
        self.git('config', 'user.email', 'test@example.invalid')
        header = self.root / 'Telegram/SourceFiles/core/version.h'
        header.parent.mkdir(parents=True)
        header.write_text('constexpr auto AppVersion = 7003001;\n'
            'constexpr auto AppVersionStr = "7.3.1";\n'
            'constexpr auto AppReleaseRevision = 0;\n')
        self.commit('base')
        self.git('tag', 'pre-release-v7.3.0-1')
        (self.root / 'feature.cpp').write_text('feature')
        self.commit('candidate')
        self.reviewed = self.git('rev-parse', 'HEAD')
        self.tag = 'pre-release-v7.3.1-2'

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), '-c', 'commit.gpgsign=false',
            '-c', 'tag.gpgsign=false', *args], text=True, stderr=subprocess.PIPE).strip()

    def commit(self, message):
        self.git('add', '.')
        self.git('commit', '-qm', message)

    def document(self, tag=None):
        tag = tag or self.tag
        metadata = {'tag': tag, 'previous_tag': 'pre-release-v7.3.0-1',
            'reviewed_commit': self.reviewed, 'date': '2026-10-19', 'upstreams': {
                source: {'from': self.reviewed, 'to': self.reviewed,
                    'url': 'https://github.com/example/source/commit/' + self.reviewed}
                for source in ('ayu', 'telegram')}}
        result = '# AywGram ' + tag.removeprefix('pre-release-v') + '\n\n'
        result += '<!-- ayw-release ' + json.dumps(metadata) + ' -->\n\n'
        for section in notes.SECTIONS:
            result += '## ' + section + '\n\n'
            if section == notes.SECTIONS[1]:
                result += '- [Contributor](https://github.com/example/contribution)\n\n'
            else:
                word = 'OWN-FIX' if section == 'AywGram' else 'UPSTREAM-FIX'
                result += '### 简体中文\n\n- 修复 ' + word + '\n\n### English\n\n- Fix ' + word + '\n\n'
        return result

    def prepare(self):
        path = self.root / notes.document_path(self.tag)
        path.parent.mkdir(parents=True)
        path.write_text(self.document(), encoding='utf-8')
        (self.root / 'changelog.txt').write_text(notes.changelog(self.root), encoding='utf-8')
        self.commit('release notes')

    def test_ready_source_and_own_only_changelog(self):
        self.prepare()
        notes.validate(self.root, self.tag)
        result = notes.changelog(self.root)
        self.assertIn('7.3.1-2 (19.10.26)', result)
        self.assertIn('OWN-FIX', result)
        self.assertIn('Contributor', result)
        self.assertNotIn('UPSTREAM-FIX', result)
        self.assertEqual(result, (self.root / 'changelog.txt').read_text())

    def test_new_substantive_change_requires_review(self):
        self.prepare()
        (self.root / 'feature.cpp').write_text('unreviewed')
        self.commit('new behavior')
        with self.assertRaisesRegex(ValueError, 'Substantive'):
            notes.validate(self.root, self.tag)

    def test_missing_notes_and_stale_changelog_stop_release(self):
        with self.assertRaises(subprocess.CalledProcessError):
            notes.validate(self.root, self.tag)
        self.prepare()
        (self.root / 'changelog.txt').write_text('incorrect')
        self.commit('stale changelog')
        with self.assertRaisesRegex(ValueError, 'stale'):
            notes.validate(self.root, self.tag)

    def test_tag_and_bilingual_content_are_required(self):
        with self.assertRaisesRegex(ValueError, 'does not match'):
            notes.parse_document(self.document(), 'pre-release-v7.3.1-3')
        with self.assertRaisesRegex(ValueError, 'Both language'):
            notes.parse_document(self.document().replace('### English', '### Other'), self.tag)

    def test_ci_updates_preserve_editorial_and_suffix(self):
        initial = notes.compose('User summary\n\n## Ayu\nUpstream', 'Building')
        initial = initial.replace('User summary', 'Edited user summary') + '\nMaintainer annotation\n'
        updated = notes.compose('Original source', 'Success', initial)
        self.assertIn('Edited user summary', updated)
        self.assertIn('Maintainer annotation', updated)
        self.assertNotIn('Original source', updated)
        self.assertNotIn('Building', updated)
        self.assertEqual(updated.count(notes.CI_START), 1)
        self.assertEqual(updated, notes.compose('ignored', 'Success', updated))

    def test_ambiguous_ci_blocks_are_rejected(self):
        for existing in ('old unmarked body', notes.CI_END + notes.CI_START,
                notes.CI_START + notes.CI_END + notes.CI_START):
            with self.subTest(body=existing), self.assertRaises(ValueError):
                notes.compose('summary', 'status', existing)

    def test_empty_history_and_revisioned_parsers(self):
        self.assertEqual(notes.changelog(self.root), notes.INTRO)
        for relative, name in (('tools/maintenance/generate_changelog.py', 'page'),
                ('Telegram/build/changelog2appstream.py', 'appstream')):
            spec = importlib.util.spec_from_file_location(name, notes.ROOT / relative)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            if name == 'page':
                self.assertIn('AywGram', module.build_html([]))
                entries = module.parse_changelog('7.3.1-2 (19.10.26)\n\n- 修复 Fix\n')
                self.assertEqual(entries[0]['version'], '7.3.1-2')
            else:
                path = self.root / 'empty.txt'
                path.write_text(notes.INTRO)
                self.assertEqual(module.parse_changelog(path), [])
                path.write_text('7.3.1-2 (19.10.26)\n\n- 修复 Fix\n')
                self.assertEqual(module.parse_changelog(path)[0][0], '7.3.1-2')


if __name__ == '__main__':
    unittest.main()
