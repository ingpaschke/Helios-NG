"""Check the native C driver's handling of compiler and assembler results."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class DriverTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.work = Path(directory.name)
        self.env = dict(os.environ, C_COMPILER="/bin/false",
                        ASSEMBLER=str(build_dir / "asm"), LINKER=str(build_dir / "asm"))
        self.env.pop("C_NONSTANDARD", None)
        self.env.pop("NEW_NCC", None)

    def run_driver(self, *args):
        return subprocess.run([build_dir / "c", *args], cwd=self.work,
                              env=self.env, capture_output=True, timeout=10)

    def test_failed_assembly_preserves_output(self):
        (self.work / "bad.s").write_text("  no_such_opcode 1\n")
        obj = self.work / "bad.o"
        obj.write_bytes(b"previous object")
        result = self.run_driver("-c", "bad.s")
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertEqual(obj.read_bytes(), b"previous object")

    def test_failed_compile_does_not_assemble_a_previous_output(self):
        (self.work / "bad.c").touch()
        (self.work / "next.c").touch()
        assembly = self.work / "bad.s"
        assembly.write_text("  ldc 23\n")
        result = self.run_driver("-c", "bad.c", "next.c")
        self.assertNotEqual(result.returncode, 0, result.stderr)
        self.assertEqual(assembly.read_text(), "  ldc 23\n")
        self.assertFalse((self.work / "bad.o").exists())

    def test_failed_pipe_preserves_objects_and_cleans_staging(self):
        self.env["C_NONSTANDARD"] = "1"
        (self.work / "bad.c").touch()
        obj = self.work / "bad.o"
        for existing in (False, True):
            with self.subTest(existing=existing):
                if existing:
                    obj.write_bytes(b"previous object")
                result = self.run_driver("-c", "bad.c")
                self.assertNotEqual(result.returncode, 0, result.stderr)
                if existing:
                    self.assertEqual(obj.read_bytes(), b"previous object")
                else:
                    self.assertFalse(obj.exists())
                self.assertEqual(list(self.work.glob(".c-*")), [])

    def test_pipe_assembles_preprocessed_c(self):
        compiler = self.work / "compiler"
        compiler.write_text("#!/bin/sh\nprintf '  ldc 23\\n'\n")
        compiler.chmod(0o755)
        self.env.update(C_COMPILER=str(compiler), C_NONSTANDARD="1")
        (self.work / "source.i").touch()
        output = self.work / "named-output"
        output.mkdir()
        obj = output / "result.o"
        obj.write_bytes(b"previous object")
        result = self.run_driver("-c", "source.i", "-o", "named-output/result.o")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotEqual(obj.read_bytes(), b"previous object")
        self.assertGreater(obj.stat().st_size, 0)
        self.assertEqual(list(output.glob(".c-*")), [])


if __name__ == "__main__":
    build_dir = Path(sys.argv.pop(1)).resolve()
    unittest.main()
