import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest.mock import patch


SPEC = importlib.util.spec_from_file_location("ci_cache", Path(__file__).with_name("ci_cache.py"))
CACHE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CACHE)


class CacheKeyTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        subprocess.run(["git", "init", "-q", str(self.root)], check=True)
        self.files = {
            "Telegram/build/prepare/prepare.py": "recipe",
            "Telegram/build/prepare/mac.sh": "wrapper",
            "Telegram/build/qt_version.py": "6.11.2",
            "Telegram/build/docker/centos_env/Dockerfile": "FROM rockylinux:8",
            "Telegram/build/docker/centos_env/poetry.lock": "locked",
            "Telegram/SourceFiles/main.cpp": "application",
        }
        for name, content in self.files.items():
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content)
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)

    def test_source_change_does_not_invalidate_dependencies(self):
        before = CACHE.dependency_hash(self.root, "windows")
        (self.root / "Telegram/SourceFiles/main.cpp").write_text("new source")
        self.assertEqual(before, CACHE.dependency_hash(self.root, "windows"))

    def test_qt_and_prepare_changes_invalidate_dependencies(self):
        for name in list(self.files)[:3]:
            with self.subTest(name=name):
                before = CACHE.dependency_hash(self.root, "mac")
                (self.root / name).write_text("modified " + name)
                self.assertNotEqual(before, CACHE.dependency_hash(self.root, "mac"))

    def test_generated_dockerfile_is_hashed_after_generation(self):
        before = CACHE.dependency_hash(self.root, "linux")
        (self.root / "Telegram/build/docker/centos_env/Dockerfile").write_text("FROM new-image")
        self.assertNotEqual(before, CACHE.dependency_hash(self.root, "linux"))

    def test_docker_environment_and_temporary_files_are_ignored(self):
        before = CACHE.dependency_hash(self.root, "linux")
        context = self.root / "Telegram/build/docker/centos_env"
        for name in (".venv/lib/data", "__pycache__/cached.pyc", "scratch.tmp"):
            path = context / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("temporary")
        subprocess.run(["git", "-C", str(self.root), "add", "."], check=True)
        self.assertEqual(before, CACHE.dependency_hash(self.root, "linux"))

    def test_toolchain_and_architecture_bound_restore_prefix(self):
        baseline = CACHE.make_keys("windows", "arm64", "MSVC 19", "deps", "1", "1")
        for arch, toolchain in (("x64", "MSVC 19"), ("arm64", "MSVC 20")):
            changed = CACHE.make_keys("windows", arch, toolchain, "deps", "1", "1")
            self.assertNotEqual(baseline["prefix"], changed["prefix"])
            self.assertNotEqual(baseline["app-prefix"], changed["app-prefix"])

    def test_rerun_has_new_key_and_same_restore_prefix(self):
        first = CACHE.make_keys("mac", "arm64", "Xcode", "deps", "10", "1")
        for run_id, attempt in (("10", "2"), ("11", "1")):
            second = CACHE.make_keys("mac", "arm64", "Xcode", "deps", run_id, attempt)
            self.assertNotEqual(first["app-key"], second["app-key"])
            self.assertEqual(first["app-prefix"], second["app-prefix"])


class CacheStatsTests(unittest.TestCase):
    def test_small_ccache_update_is_saved(self):
        changed, stats = CACHE.parse_stats("ccache", "cache_miss\t1\nlocal_storage_write\t2\n")
        self.assertGreater(changed, 0)
        self.assertEqual(stats["cache_miss"], 1)

    def test_sccache_uses_successful_writes_not_uncacheable_requests(self):
        raw = json.dumps({"stats": {"cache_writes": 0, "requests_not_cacheable": 600}})
        self.assertEqual(CACHE.parse_stats("sccache", raw)[0], 0)

    def test_ccache_miss_without_storage_write_is_not_saved(self):
        raw = "cache_miss\t1\nlocal_storage_write\t0\n"
        self.assertEqual(CACHE.parse_stats("ccache", raw)[0], 0)

    def test_malformed_statistics_are_not_zero_misses(self):
        for raw in ("", "cache_miss\tnot-a-number", "cache_miss\t-1\nlocal_storage_write\t1"):
            with self.subTest(raw=raw), self.assertRaises((KeyError, ValueError)):
                CACHE.parse_stats("ccache", raw)
        with self.assertRaises(KeyError):
            CACHE.parse_stats("sccache", '{"stats": {}}')

    def test_changed_output_is_absent_when_statistics_fail(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "outputs"
            with patch.dict(os.environ, GITHUB_OUTPUT=str(output)), \
                    patch.object(CACHE, "STATE", Path(directory)), \
                    patch.object(CACHE, "capture", return_value="invalid"), \
                    self.assertRaises(KeyError):
                CACHE.stats(type("Args", (), {"engine": "ccache"})())
            self.assertFalse(output.exists())
            self.assertIn("error", json.loads((Path(directory) / "stats.json").read_text()))


class CacheInstallerTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("sh"), "POSIX shell is required")
    def test_mac_wrapper_cleans_xcode_environment_and_forwards_exit_status(self):
        with tempfile.TemporaryDirectory() as directory:
            tools = Path(directory) / "tools with spaces"
            tools.mkdir()
            compiler = tools / "real compiler"
            compiler.write_text(
                '#!/bin/sh\n'
                'env | sort\n'
                'printf "arg=%s\\n" "$@"\n'
                'exit 17\n', encoding="utf-8", newline="\n")
            compiler.chmod(0o755)
            cache = tools / "ccache"
            cache.write_text('#!/bin/sh\nexec "$@"\n', encoding="utf-8", newline="\n")
            cache.chmod(0o755)
            with patch.object(CACHE, "TOOLS", tools), \
                    patch.object(CACHE, "capture", return_value=compiler.as_posix()), \
                    patch.object(CACHE, "append_env_file"):
                CACHE.mac_wrappers(None)
            foreign = {
                "IPHONEOS_DEPLOYMENT_TARGET": "26.5",
                "IOS_SIMULATOR_DEPLOYMENT_TARGET": "26.5",
                "TVOS_DEPLOYMENT_TARGET": "26.5",
                "WATCHOS_DEPLOYMENT_TARGET": "26.5",
                "XROS_DEPLOYMENT_TARGET": "26.5",
                "DRIVERKIT_DEPLOYMENT_TARGET": "25.5",
            }
            for language in ("clang", "clang++"):
                with self.subTest(language=language):
                    result = subprocess.run(
                        ["sh", (tools / f"cached-{language}").as_posix(),
                            "-dM", "-E", "source with spaces.cpp", ""],
                        env={**os.environ, **foreign, "MACOSX_DEPLOYMENT_TARGET": "11.0",
                            "SDKROOT": "/Xcode Path/MacOSX.sdk"},
                        capture_output=True, text=True, check=False)
                    self.assertEqual(result.returncode, 17, result.stderr)
                    for variable in foreign:
                        self.assertNotIn(variable + "=", result.stdout)
                    self.assertIn("MACOSX_DEPLOYMENT_TARGET=11.0\n", result.stdout)
                    self.assertIn("SDKROOT=/Xcode Path/MacOSX.sdk\n", result.stdout)
                    self.assertTrue(result.stdout.endswith(
                        "arg=-dM\narg=-E\narg=source with spaces.cpp\narg=\n"))

    def test_checksum_failure_cannot_install_binary(self):
        with tempfile.TemporaryDirectory() as directory:
            tools = Path(directory)
            def download(url, path):
                Path(path).write_bytes(b"not the official archive")
            with patch.object(CACHE, "TOOLS", tools), \
                    patch.object(CACHE.urllib.request, "urlretrieve", side_effect=download), \
                    self.assertRaisesRegex(ValueError, "SHA-256 mismatch"):
                CACHE.install(type("Args", (), {"target": "windows-arm64"})())
            self.assertFalse((tools / "sccache.exe").exists())

    def test_mac_wrapper_preserves_arguments_and_real_compiler(self):
        with tempfile.TemporaryDirectory() as directory:
            tools = Path(directory) / "tools with spaces"
            with patch.object(CACHE, "TOOLS", tools), \
                    patch.object(CACHE, "capture", return_value="/Xcode Path/clang++"), \
                    patch.object(CACHE, "append_env_file") as output:
                CACHE.mac_wrappers(None)
            wrapper = (tools / "cached-clang++").read_text()
            self.assertIn("'/Xcode Path/clang++'", wrapper)
            self.assertTrue(wrapper.endswith('"$@"\n'))
            self.assertEqual(output.call_args.args[1]["REAL_CXX_COMPILER"], "/Xcode Path/clang++")


if __name__ == "__main__":
    unittest.main()
