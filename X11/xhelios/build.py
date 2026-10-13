#!/usr/bin/env python3
"""Cross-build the recovered Helios X drivers without changing an installation."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

HERE = Path(__file__).resolve().parent
WORK = HERE.parent


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--sysroot', type=Path, default=Path('/opt/llvm-t800/sysroots/helios'))
    ap.add_argument('--clang', type=Path, default=Path(os.environ.get(
        'CLANG', '/opt/llvm-t800/bin/clang')))
    ap.add_argument('--cpu', choices=['t425', 't800'], default='t425')
    ap.add_argument('--asm', type=Path, default=Path('/opt/llvm-t800/bin/asm'),
                    help='the Helios assembler and linker')
    ap.add_argument('--ampp', type=Path, default=Path('/opt/llvm-t800/bin/ampp'),
                    help='the Helios macro preprocessor')
    ap.add_argument('--work', type=Path, default=HERE / 'build',
                    help='build directory (the drivers go to WORK/CPU)')
    ap.add_argument('--modend', type=Path,
                    help='the module end object (default SYSROOT/lib/modend.o)')
    args = ap.parse_args()
    root = args.sysroot.resolve()
    out = args.work / args.cpu
    out.mkdir(parents=True, exist_ok=True)
    asm = args.asm
    ampp = args.ampp
    vendor = HERE / 'vendor/helios'
    commands = []

    def run(cmd, name):
        cmd = [str(x) for x in cmd]
        commands.append(cmd)
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        result = subprocess.run(cmd, cwd=HERE, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, text=True, errors='replace')
        (out / (name + '.log')).write_text(result.stdout)
        if result.returncode or any(s in result.stdout.lower() for s in
                                   ['undefined symbol', 'error:', 'fatal error']):
            print(result.stdout[-6000:], file=sys.stderr)
            raise RuntimeError('failed: ' + name)

    flags = [args.clang, '--target=t800-helios', '-mcpu=' + args.cpu,
             '--sysroot=' + str(root), '-std=gnu89', '-Os', '-S',
             '-I' + str(vendor), '-I' + str(HERE / 'vendor/include'),
             '-I' + str(HERE / 'src')]

    def compile_c(source, name, device=False):
        extra = ['-mllvm', '-t800-helios-nomodule', '-mllvm',
                 '-t800-helios-nodata', '-mllvm', '-t800-helios-stack-check=false',
                 '-mllvm', '-t800-helios-proc-names=false'] if device else []
        # md.h refers to KeybdCtrl, defined by mdkeyboard.h before including it.
        if name == 'mouse':
            extra += ['-include', str(vendor / 'mdkeyboard.h')]
        if device:
            # A driver stores its Device * in DCB.Device, which Helios-NG's
            # device.h declares as an MPtr (char *; Helios 1.3: Device *),
            # and the keyboard driver passes char ** for its CARD8 **
            # modifier map.
            extra += ['-Wno-error=incompatible-pointer-types']
        run(flags + extra + [source, '-o', out / (name + '.s')], name + '-cc')
        run([asm, '-p', '-o', out / (name + '.o'), out / (name + '.s')], name + '-asm')
        return out / (name + '.o')

    # AMPP treats CR as a token; the installed SDK files have DOS endings.
    macros = out / 'ampp'
    macros.mkdir(exist_ok=True)
    for source in (root / 'include/ampp').iterdir():
        if source.is_file():
            (macros / source.name).write_bytes(source.read_bytes().replace(b'\r\n', b'\n'))
    run([ampp, '-dhelios.TRAN', '1', '-i' + str(macros) + '/',
         '-o' + str(out / 'device.s'), macros / 'basic.m',
         vendor / 'devices/device.a'], 'device-ampp')
    run([asm, '-p', '-o', out / 'device.o', out / 'device.s'], 'device-asm')
    for name in ['newatw', 'mouse', 'keyboard']:
        obj = compile_c(vendor / 'devices' / (name + '.c'), name, device=True)
        run([asm, '-o', out / (name + '.d'), out / 'device.o', obj,
             args.modend or root / 'lib/modend.o'] +
            [root / 'lib' / (lib + '.def') for lib in
             ['kernel', 'syslib', 'util', 'posix', 'clib']], name + '-link')
        print(out / (name + '.d'))


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError) as exc:
        sys.exit(str(exc))
