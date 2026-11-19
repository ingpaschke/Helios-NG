"""Regression tests for the Linux build directory and source snapshot."""

import fcntl
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


TOOLS = Path(__file__).resolve().parent


class BuildTreeTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.base = Path(temp.name)
        self.repo = self.base / "repo"
        self.repo.mkdir()
        self.tools = self.repo / "tools/build-t800"
        self.tools.mkdir(parents=True)
        shutil.copy2(TOOLS / "build.sh", self.tools)
        # The script checks its SDK before selecting an output directory.
        self.sdk = self.base / "sdk"
        for name in ("bin/hcc", "bin/clang", "sysroots/helios/lib/llvmrt-generic.lib",
                     "sysroots/helios/lib/llvmrt-t4.lib"):
            path = self.sdk / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch(mode=0o755)

    def build(self, out):
        return subprocess.run([self.tools / "build.sh", out], capture_output=True,
                              env={**os.environ, "T800": str(self.sdk)})

    def test_unrelated_output(self):
        out = self.base / "project"
        for name in ("src", "logs", "host"):
            (out / name).mkdir(parents=True)
        source = out / "src/project.c"
        source.write_text("keep")
        result = self.build(out)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"no .build-t800 marker", result.stderr)
        self.assertEqual(source.read_text(), "keep")
        self.assertFalse((out / ".build-t800").exists())

    def test_concurrent_build(self):
        out = self.base / "busy"
        out.mkdir()
        (out / ".build-t800").touch()
        directory = os.open(out, os.O_RDONLY)
        try:
            fcntl.flock(directory, fcntl.LOCK_EX)
            result = self.build(out)
        finally:
            os.close(directory)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"cannot lock build directory", result.stderr)
        self.assertEqual(list(out.iterdir()), [out / ".build-t800"])

    def test_source_checkout_as_output(self):
        for out in (self.repo, self.base):
            with self.subTest(out=out):
                (out / ".build-t800").touch()
                result = self.build(out)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"contains the Helios checkout", result.stderr)

    def test_nested_copy(self):
        output = self.repo / "local/current/src"
        fixtures = {
            ".git": "gitdir: external",
            "local/current/.build-t800": "",
            "local/previous/.build-t800": "",
            "local/previous/logs/report": "old",
            "local/current/host/tool": "old",
            "build-t800/other": "old",
            "source/lib.c": "contents",
            "source/TRAN/program.o": "product",
            "source/TRAN/RCS/firmware,v": "archive",
            "source/TRAN/firmware": "input",
        }
        for name, contents in fixtures.items():
            path = self.repo / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents)
        (self.repo / "headers").symlink_to("source")
        manifest = self.base / "manifest"
        products = self.base / "products"
        subprocess.run([TOOLS / "headco.py", self.repo, output, "--manifest", manifest,
                        "--products", products], check=True, capture_output=True)
        self.assertEqual((output / "source/lib.c").read_text(), "contents")
        self.assertEqual(os.readlink(output / "headers"), "source")
        self.assertEqual(manifest.read_text().splitlines(), [
            "worktree\theaders", "worktree-rcs\tsource/TRAN/firmware",
            "worktree\tsource/lib.c",
        ])
        self.assertEqual(products.read_text(), "source/TRAN/program.o\n")
        for name in (".git", "tools", "local", "build-t800", "source/TRAN/RCS"):
            self.assertFalse((output / name).exists())

    def test_copy_errors(self):
        for src, dest in ((self.repo, self.repo), (self.repo, self.base),
                          (self.base / "absent", self.base / "output")):
            with self.subTest(src=src, dest=dest):
                result = subprocess.run([TOOLS / "headco.py", src, dest], capture_output=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)


if __name__ == "__main__":
    unittest.main()
