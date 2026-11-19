"""Exercise the build-machine generators using their command-line interfaces."""

import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)

    def run_tool(self, name, *args):
        return subprocess.run(
            [build_dir / name, *args], cwd=self.work, capture_output=True
        )

    def test_keyword_table(self):
        result = self.run_tool("keyform")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("WORD dicttab[]", (self.work / "keytab.c").read_text())

    def test_firmware_chunks(self):
        for length in (1, 15, 16, 17, 32, 256):
            with self.subTest(length=length):
                firmware = bytes(range(length))
                (self.work / "firmware").write_bytes(firmware)
                result = self.run_tool("gensquirt", "firmware", "driver.c")
                self.assertEqual(result.returncode, 0, result.stderr)
                source = (self.work / "driver.c").read_text()
                chunks = re.findall(r'data\[(\d+)\] =\s+"([^"]*)";', source)
                expected = [
                    (str(i // 16), "".join(r"\x%02x" % b for b in firmware[i : i + 16]))
                    for i in range(0, length, 16)
                ]
                self.assertEqual(chunks, expected)
                self.assertIn("size = %d;" % length, source)
                self.assertIn("nstr = %d;" % len(expected), source)

    def test_empty_firmware(self):
        (self.work / "firmware").touch()
        result = self.run_tool("gensquirt", "firmware", "driver.c")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.work / "driver.c").exists())

    @unittest.skipUnless(Path("/dev/full").exists(), "requires /dev/full")
    def test_write_errors(self):
        (self.work / "keytab.c").symlink_to("/dev/full")
        (self.work / "firmware").write_bytes(b"\x80")
        for name, args in (("keyform", ()), ("gensquirt", ("firmware", "/dev/full"))):
            with self.subTest(tool=name):
                result = self.run_tool(name, *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)


class FaultHeaderTests(unittest.TestCase):
    def test_fault_database(self):
        database = Path(__file__).resolve().parents[2] / "fault/master.fdb"
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            result = subprocess.run([build_dir / "gencodes", database], cwd=work,
                                    capture_output=True, check=True)
            for header in ("codes.h", "errno.h", "signal.h"):
                self.assertGreater((work / header).stat().st_size, 0)
            self.assertFalse(result.stderr)

    def test_fault_database_errors(self):
        with tempfile.TemporaryDirectory() as folder:
            work = Path(folder)
            (work / "directory").mkdir()
            (work / "unfinished").write_text("#{\n")
            (work / "bad-output").write_text("#!missing/header.h\n")
            cases = ["missing-input", "directory", "unfinished", "bad-output"]
            if Path("/dev/full").exists():
                (work / "full").write_text("#!/dev/full\n##header\n")
                (work / "switch").write_text("#!/dev/full\n##header\n#!next.h\n")
                cases.extend(["full", "switch"])
            for name in cases:
                with self.subTest(database=name):
                    result = subprocess.run([build_dir / "gencodes", name], cwd=work,
                                            capture_output=True, timeout=5)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertTrue(result.stderr)


if __name__ == "__main__":
    build_dir = Path(sys.argv.pop(1)).resolve()
    unittest.main()
