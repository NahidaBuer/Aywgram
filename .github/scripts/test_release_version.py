#!/usr/bin/env python3

import importlib.util
import pathlib
import tempfile
import unittest


SCRIPT = pathlib.Path(__file__).with_name("release_version.py")
SPEC = importlib.util.spec_from_file_location("release_version", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class ReleaseVersionTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.header = pathlib.Path(self.directory.name) / "version.h"

    def tearDown(self):
        self.directory.cleanup()

    def write_header(self, version=7001003, name="7.1.3", revision=0):
        self.header.write_text(
            f"constexpr auto AppVersion = {version};\n"
            f'constexpr auto AppVersionStr = "{name}";\n'
            f"constexpr auto AppReleaseRevision = {revision};\n",
            encoding="utf-8",
        )

    def resolve(self, tag):
        return MODULE.resolve_version(tag, self.header)

    def test_resolves_three_part_version(self):
        self.write_header()
        _, result = self.resolve("pre-release-v7.1.3-1")
        self.assertEqual(result["app_version"], 7001003)
        self.assertEqual(result["revision"], 1)
        self.assertEqual(result["version_name"], "7.1.3-1")

    def test_resolves_two_part_version(self):
        self.write_header(version=7002000, name="7.2")
        _, result = self.resolve("pre-release-v7.2-12")
        self.assertEqual(result["version_name"], "7.2-12")

    def test_rejects_source_name_mismatch(self):
        self.write_header()
        with self.assertRaises(MODULE.ReleaseVersionError):
            self.resolve("pre-release-v7.1.4-1")

    def test_rejects_numeric_version_mismatch(self):
        self.write_header(version=7001004)
        with self.assertRaises(MODULE.ReleaseVersionError):
            self.resolve("pre-release-v7.1.3-1")

    def test_rejects_revision_zero(self):
        self.write_header()
        with self.assertRaises(MODULE.ReleaseVersionError):
            self.resolve("pre-release-v7.1.3-0")

    def test_rejects_revision_above_limit(self):
        self.write_header()
        with self.assertRaises(MODULE.ReleaseVersionError):
            self.resolve("pre-release-v7.1.3-100")

    def test_rejects_revision_leading_zero(self):
        self.write_header()
        with self.assertRaises(MODULE.ReleaseVersionError):
            self.resolve("pre-release-v7.1.3-01")

    def test_applies_revision_without_changing_base_version(self):
        self.write_header()
        source, result = self.resolve("pre-release-v7.1.3-9")
        MODULE.apply_revision(self.header, source, result["revision"])
        updated = self.header.read_text(encoding="utf-8")
        self.assertIn("AppVersion = 7001003", updated)
        self.assertIn('AppVersionStr = "7.1.3"', updated)
        self.assertIn("AppReleaseRevision = 9", updated)

    def test_untagged_build_uses_revision_zero(self):
        self.write_header()
        _, result = self.resolve("")
        self.assertEqual(result["version_name"], "7.1.3")
        self.assertEqual(result["revision"], 0)


if __name__ == "__main__":
    unittest.main()
