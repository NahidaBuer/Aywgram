import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class DependencyPruningTests(unittest.TestCase):
    def prune(self, platform):
        if platform == "mac" and sys.platform != "darwin":
            self.skipTest("The macOS workflow uses BSD find permissions syntax")
        workflow = (ROOT / f".github/workflows/{platform}.yml").read_text()
        if platform == "mac":
            command = next(line.strip().removeprefix("run: ")
                for line in workflow.splitlines() if "run: find Libraries " in line)
        else:
            command = next(shlex.split(line.strip())[2]
                for line in workflow.splitlines() if "bash -lc 'find" in line)
        scratch = ROOT / "out/tmp/ci-pruning"
        scratch.mkdir(parents=True, exist_ok=True)
        temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        keep = [
            "local/src/wallet_engine/wallet_engine.cpp",
            "tdesktop_rust/out/src/wallet_engine/wallet_engine.cpp",
            "local/include/wallet_engine/wallet_engine.hpp",
            "cache_keys/tdesktop_rust",
            "local/lib/libtdesktop_rust.a",
        ]
        for checkout in ("tlottie", "wallet-engine"):
            keep.extend(f"{checkout}/{name}" for name in (
                "Cargo.toml", "Cargo.lock", "src/lib.rs", "src/module with spaces.rs",
                "bindgen/cpp/bindgen/Cargo.toml", ".git/HEAD"))
        discard = ["other/build/scratch.cpp", "other/build/scratch.o", "other/build/scratch.log"]
        for name in keep + discard:
            path = root / "Libraries" / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture\n")
        subprocess.run(["bash", "-c", command], cwd=root,
            env={**os.environ, "LibrariesPath": str(root / "Libraries")}, check=True)
        self.assertEqual([name for name in keep if not (root / "Libraries" / name).is_file()], [])
        self.assertEqual([name for name in discard if (root / "Libraries" / name).exists()], [])

    def test_mac_keeps_wallet_bindings_and_rust_rebuild_inputs(self):
        self.prune("mac")

    def test_windows_keeps_wallet_bindings_and_rust_rebuild_inputs(self):
        self.prune("win")


if __name__ == "__main__":
    unittest.main()
