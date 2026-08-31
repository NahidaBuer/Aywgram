#!/usr/bin/env python3

import importlib.util
import json
import pathlib
import io
import tarfile
import tempfile
import types
import unittest
import zipfile


SCRIPT = pathlib.Path(__file__).with_name("update_release_metadata.py")
SPEC = importlib.util.spec_from_file_location("update_release_metadata", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MetadataTest(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.root = pathlib.Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def asset(self, target, app_version, revision=1, name=None):
        base = "7.2" if app_version == 7002000 else "7.1.4"
        version_name = f"{base}-{revision}"
        release = f"pre-release-v{version_name}"
        suffix = "tar.gz" if target.startswith("linux-") else "zip"
        name = name or f"AywGram-v{version_name}-{target}.{suffix}"
        return {
            "app_version": app_version,
            "revision": revision,
            "version_name": version_name,
            "release": release,
            "url": f"https://github.com/NahidaBuer/AywGram/releases/download/{release}/{name}",
            "format": MODULE.TARGET_FORMATS[target],
            "size": 123,
            "sha256": "a" * 64,
        }

    def write(self, name, value):
        path = self.root / name
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def merge(self, base=None, fragments=(), release="v7.2.0"):
        output = self.root / "output.json"
        args = types.SimpleNamespace(
            base=str(base) if base else None,
            feed_release=release,
            generated_at="2026-09-01T12:00:00Z",
            fragment=[str(value) for value in fragments],
            output=str(output),
        )
        MODULE.merge_metadata(args)
        return json.loads(output.read_text(encoding="utf-8"))

    def fragment(self, target, asset, name="fragment.json"):
        return self.write(name, {"target": target, "asset": asset})

    def base(self, targets):
        return self.write("base.json", {
            "schema": 1,
            "feed_release": "v7.1.4",
            "generated_at": "2026-08-01T00:00:00Z",
            "targets": targets,
        })

    def test_first_generation(self):
        fragment = self.fragment(
            "windows-x86_64", self.asset("windows-x86_64", 7002000))
        result = self.merge(fragments=[fragment])
        self.assertEqual(
            result["targets"]["windows-x86_64"]["app_version"], 7002000)

    def test_accepts_prerelease_asset_tag_and_metadata_carrier(self):
        asset = self.asset(
            "windows-x86_64",
            7002000,
            revision=1,
        )
        fragment = self.fragment("windows-x86_64", asset)
        result = self.merge(
            fragments=[fragment],
            release="update-metadata",
        )
        self.assertEqual(result["feed_release"], "update-metadata")
        self.assertEqual(
            result["targets"]["windows-x86_64"]["release"],
            "pre-release-v7.2-1",
        )

    def test_rejects_unsafe_release_tag(self):
        with self.assertRaises(MODULE.MetadataError):
            MODULE.validate_release("release/../../main")

    def test_rejects_release_that_does_not_match_version(self):
        asset = self.asset("windows-x86_64", 7002000)
        asset["release"] = "pre-release-v7.2-2"
        asset["url"] = asset["url"].replace(
            "pre-release-v7.2-1", "pre-release-v7.2-2")
        with self.assertRaises(MODULE.MetadataError):
            MODULE.validate_asset("windows-x86_64", asset)

    def test_preserves_old_target(self):
        base = self.base({"linux-x86_64": self.asset("linux-x86_64", 7001004)})
        fragment = self.fragment(
            "windows-x86_64", self.asset("windows-x86_64", 7002000))
        result = self.merge(base, [fragment])
        self.assertEqual(
            result["targets"]["linux-x86_64"]["app_version"], 7001004)

    def test_drops_unknown_target(self):
        base = self.base({"unsupported-target": {"legacy": True}})
        result = self.merge(base)
        self.assertNotIn("unsupported-target", result["targets"])

    def test_renders_release_download_links(self):
        metadata = self.base({
            "windows-x86_64": self.asset("windows-x86_64", 7002000),
            "linux-x86_64": self.asset("linux-x86_64", 7001004),
        })
        output = self.root / "release-notes.md"
        MODULE.render_release_notes(types.SimpleNamespace(
            metadata=str(metadata),
            output=str(output),
        ))
        notes = output.read_text(encoding="utf-8")
        self.assertIn("Windows x86_64", notes)
        self.assertIn("Linux x86_64", notes)
        self.assertIn(self.asset("windows-x86_64", 7002000)["url"], notes)
        self.assertNotIn("macOS ARM64", notes)

    def test_upgrades_target(self):
        base = self.base({"linux-x86_64": self.asset("linux-x86_64", 7001004)})
        fragment = self.fragment(
            "linux-x86_64", self.asset("linux-x86_64", 7002000), "new.json")
        result = self.merge(base, [fragment])
        self.assertEqual(
            result["targets"]["linux-x86_64"]["app_version"], 7002000)

    def test_upgrades_revision_on_same_base_version(self):
        base = self.base({
            "linux-x86_64": self.asset("linux-x86_64", 7002000, 1),
        })
        fragment = self.fragment(
            "linux-x86_64",
            self.asset("linux-x86_64", 7002000, 2),
            "revision.json",
        )
        result = self.merge(base, [fragment])
        self.assertEqual(result["targets"]["linux-x86_64"]["revision"], 2)

    def test_new_base_version_can_reset_revision(self):
        base = self.base({
            "linux-x86_64": self.asset("linux-x86_64", 7001004, 99),
        })
        fragment = self.fragment(
            "linux-x86_64",
            self.asset("linux-x86_64", 7002000, 1),
            "new-base.json",
        )
        result = self.merge(base, [fragment])
        self.assertEqual(result["targets"]["linux-x86_64"]["revision"], 1)

    def test_rejects_rollback(self):
        base = self.base({"linux-x86_64": self.asset("linux-x86_64", 7002000)})
        fragment = self.fragment(
            "linux-x86_64", self.asset("linux-x86_64", 7001004), "old.json")
        with self.assertRaises(MODULE.MetadataError):
            self.merge(base, [fragment])

    def test_same_version_requires_same_release_and_name(self):
        base = self.base({"windows-x86_64": self.asset("windows-x86_64", 7002000)})
        changed = self.asset("windows-x86_64", 7002000, name="changed.zip")
        fragment = self.fragment("windows-x86_64", changed)
        with self.assertRaises(MODULE.MetadataError):
            self.merge(base, [fragment])

    def test_same_version_accepts_same_release_and_name(self):
        asset = self.asset("windows-x86_64", 7002000)
        base = self.base({"windows-x86_64": asset})
        fragment = self.fragment("windows-x86_64", asset)
        result = self.merge(base, [fragment])
        self.assertEqual(result["targets"]["windows-x86_64"], asset)

    def test_rejects_duplicate_concurrent_target(self):
        first = self.fragment(
            "mac-arm64", self.asset("mac-arm64", 7002000), "first.json")
        second = self.fragment(
            "mac-arm64", self.asset("mac-arm64", 7002001), "second.json")
        with self.assertRaises(MODULE.MetadataError):
            self.merge(fragments=[first, second])

    def zip(self, name, members):
        path = self.root / name
        with zipfile.ZipFile(path, "w") as archive:
            for member, mode in members:
                info = zipfile.ZipInfo(member)
                info.external_attr = (mode & 0xFFFF) << 16
                archive.writestr(info, b"data")
        return path

    def tar(self, name, members):
        path = self.root / name
        with tarfile.open(path, "w:gz") as archive:
            for member, mode in members:
                info = tarfile.TarInfo(member)
                info.mode = mode
                info.size = 4
                archive.addfile(info, io.BytesIO(b"data"))
        return path

    def test_validates_windows_zip_layout(self):
        path = self.zip("windows.zip", [
            ("AywGram.exe", 0o644),
            ("Updater.exe", 0o644),
        ])
        MODULE.validate_archive(path, "windows-x86_64")

    def test_creates_fragment_from_archive(self):
        path = self.zip("AywGram-v7.2-1-windows-x86_64.zip", [
            ("AywGram.exe", 0o644),
            ("Updater.exe", 0o644),
        ])
        output = self.root / "generated-fragment.json"
        MODULE.create_fragment(types.SimpleNamespace(
            target="windows-x86_64",
            app_version="7002000",
            revision="1",
            version_name="7.2-1",
            release="pre-release-v7.2-1",
            asset=str(path),
            output=str(output),
        ))
        fragment = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(fragment["asset"]["size"], path.stat().st_size)
        self.assertEqual(len(fragment["asset"]["sha256"]), 64)

    def test_rejects_zip_path_traversal(self):
        path = self.zip("traversal.zip", [
            ("AywGram.exe", 0o644),
            ("Updater.exe", 0o644),
            ("../ready", 0o644),
        ])
        with self.assertRaises(MODULE.MetadataError):
            MODULE.validate_archive(path, "windows-x86_64")

    def test_rejects_missing_updater(self):
        path = self.zip("missing.zip", [("AywGram.exe", 0o644)])
        with self.assertRaises(MODULE.MetadataError):
            MODULE.validate_archive(path, "windows-x86_64")

    def test_rejects_linux_permissions(self):
        path = self.tar("linux.tar.gz", [
            ("AywGram", 0o755),
            ("Updater", 0o644),
        ])
        with self.assertRaises(MODULE.MetadataError):
            MODULE.validate_archive(path, "linux-x86_64")

    def test_rejects_corrupt_archive(self):
        path = self.root / "corrupt.tar.gz"
        path.write_bytes(b"not a tar archive")
        with self.assertRaises(MODULE.MetadataError):
            MODULE.validate_archive(path, "linux-x86_64")

    def test_rejects_expanded_size_limit(self):
        path = self.tar("large.tar.gz", [
            ("AywGram", 0o755),
            ("Updater", 0o755),
        ])
        original = MODULE.MAX_EXPANDED_SIZE
        MODULE.MAX_EXPANDED_SIZE = 7
        try:
            with self.assertRaises(MODULE.MetadataError):
                MODULE.validate_archive(path, "linux-x86_64")
        finally:
            MODULE.MAX_EXPANDED_SIZE = original

    def test_client_uses_fixed_release_url(self):
        source = SCRIPT.parents[2] / "Telegram/SourceFiles/core/update_checker.cpp"
        text = source.read_text(encoding="utf-8")
        self.assertNotIn("api.github.com", text)
        self.assertIn(
            "releases/latest/download/update-metadata.json",
            text,
        )

    def test_staging_manifest_uses_release_tuple(self):
        source = SCRIPT.parents[2] / "Telegram/SourceFiles/core/update_unpack.cpp"
        text = source.read_text(encoding="utf-8")
        self.assertIn('u"app_version"_q, asset.appVersion', text)
        self.assertIn('u"revision"_q, asset.revision', text)
        self.assertIn('u"version_name"_q, asset.versionName', text)
        self.assertNotIn('u"version"_q, double(asset.version)', text)


if __name__ == "__main__":
    unittest.main()
