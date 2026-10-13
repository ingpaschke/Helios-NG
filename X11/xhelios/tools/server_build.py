#!/usr/bin/env python3
"""Build X11R4 in a private tree; keep the upstream checkout unmodified."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
from prepare import prepare

HERE = Path(__file__).resolve().parents[1]
WORK = HERE.parent


def sources(mit):
    entries = []
    for group in ['dix', 'ddx/mi', 'ddx/cfb', 'ddx/mfb', 'ddx/snf']:
        for src in sorted((mit / 'server' / group).glob('*.c')):
            if src.name in ['mfbplygblt.c', 'mfbimggblt.c', 'mfbpntarea.c', 'mfbtegblt.c']:
                continue
            entries.append((src, group.replace('/', '_') + '_' + src.stem, []))
    for fb in ['mfb', 'cfb']:
        entries.append((mit / 'server/ddx' / fb / (fb + 'line.c'), fb + 'seg', ['-DPOLYSEGMENT']))
    for suffix, op, colour in [('wht', '|=', 'White'), ('blak', '&=~', 'Black'), ('inv', '^=', 'Invert')]:
        entries.append((mit / 'server/ddx/mfb/mfbplygblt.c', 'mfbpgb' + suffix,
                        ['-DMFBPOLYGLYPHBLT=mfbPolyGlyphBlt' + colour, '-DOPEQ=' + op]))
        entries.append((mit / 'server/ddx/mfb/mfbpntarea.c', 'mfbpa' + suffix,
                        ['-DMFBSOLIDFILLAREA=mfbSolid' + colour + 'Area',
                         '-DMFBSTIPPLEFILLAREA=mfbStipple' + colour + 'Area',
                         '-DOPEQ=' + op, '-DEQWHOLEWORD=' + {'wht': '=~0', 'blak': '=0', 'inv': '^=~0'}[suffix]]))
        if suffix != 'inv':
            entries.append((mit / 'server/ddx/mfb/mfbimggblt.c', 'mfbigb' + suffix,
                            ['-DMFBIMAGEGLYPHBLT=mfbImageGlyphBlt' + colour, '-DOPEQ=' + op]))
            entries.append((mit / 'server/ddx/mfb/mfbtegblt.c', 'mfbte' + suffix,
                            ['-DMFBTEGLYPHBLT=mfbTEGlyphBlt' + colour,
                             '-DOP=' + ('' if suffix == 'wht' else '~'),
                             '-DCLIPTETEXT=mfbImageGlyphBlt' + colour]))
    for name in ['WaitFor', 'connection', 'access', 'io', 'osfonts', 'fonttype', 'utils', 'auth', 'mitauth']:
        entries.append((mit / 'server/os/4.2bsd' / (name + '.c'), 'os_' + name, []))
    # The extension set of the original Xhelios: X11R4's standard SHAPE,
    # Multi-Buffering and MIT-SUNDRY-NONSTANDARD, initialised by mi/miinitext.c,
    # plus MIT-SHM on a ddx-provided region (see prepare.py).
    for name in ['shape', 'multibuf', 'mitmisc', 'shm']:
        entries.append((mit / 'extensions/server' / (name + '.c'), 'ext_' + name, []))
    entries.append((mit / 'fonts/mkfontdir/fontdir.c', 'fontdir', []))
    for src in sorted((mit / 'lib/Xau').glob('*.c')):
        if src.name not in ['Autest.c']:
            entries.append((src, 'xau_' + src.stem, []))
    entries.append((mit / 'server/ddx/helios-keymap.c', 'helios_keymap', []))
    for name in ['ddx', 'runtime', 'input', 'native_events', 'config', 'trace', 'atw_copy2d']:
        entries.append((HERE / 'src' / (name + '.c'), 'helios_' + name, []))
    return entries


def diag_edits(mit):
    """Trace font loading with ErrorF (diagnostic builds only)."""
    def edit(rel, old, new):
        p = mit / rel
        t = p.read_text()
        if new in t:
            return
        if t.count(old) != 1:
            raise RuntimeError('diag edit anchor not unique in ' + rel + ': ' + old[:40])
        p.write_text(t.replace(old, new))
    f = 'server/os/4.2bsd/osfonts.c'
    edit(f, '    file = fopen(dir_file, "r");\n',
         '    file = fopen(dir_file, "r");\n'
         '    ErrorF("diag: fopen %s -> %s errno %d\\n", dir_file, file ? "ok" : "FAIL", errno);\n')
    edit(f, '\tcount = fscanf(file, "%d\\n", &i);\n',
         '\tcount = fscanf(file, "%d\\n", &i);\n'
         '\tErrorF("diag: fonts.dir header count %d entries %d\\n", count, i);\n')
    edit(f, '    status = ReadFontAlias(directory, FALSE, &table, path);\n',
         '    ErrorF("diag: fonts.dir table %d names\\n", table ? table->name.used : -1);\n'
         '    status = ReadFontAlias(directory, FALSE, &table, path);\n'
         '    ErrorF("diag: ReadFontAlias status %d\\n", status);\n')
    edit(f, '    if (!isFile) {\n        char *dot',
         '    ErrorF("diag: alias lookup in %s\\n", directory);\n'
         '    if (!isFile) {\n        char *dot')
    edit(f, '    if ((fp = OpenFontFile(table, ff->name, &typeIndex, &cookie)) == NULL)\n'
            '\treturn NullFont;\n'
            '    setbuf (fp, buf);\n'
            '    font = (fontFileReaders[typeIndex].loadFont) (fp);\n',
         '    if ((fp = OpenFontFile(table, ff->name, &typeIndex, &cookie)) == NULL) {\n'
         '\tErrorF("diag: open %s%s FAIL errno %d\\n", table->directory, ff->name, errno);\n'
         '\treturn NullFont;\n'
         '    }\n'
         '    setbuf (fp, buf);\n'
         '    font = (fontFileReaders[typeIndex].loadFont) (fp);\n'
         '    ErrorF("diag: load %s%s -> %s\\n", table->directory, ff->name, font ? "ok" : "FAIL");\n')
    # Keep the conditional: an unbraced trace before FatalError made the
    # first diagnostic builds abort even after the font had loaded.
    m = 'server/dix/main.c'
    edit(m, '\tif (SetDefaultFontPath(defaultFontPath) != Success)\n'
            '\t    ErrorF("failed to set default font path \'%s\'", defaultFontPath);\n',
         '\ti = SetDefaultFontPath(defaultFontPath);\n'
         '\tErrorF("diag: SetDefaultFontPath %s status %d\\n", defaultFontPath, i);\n'
         '\tif (i != Success)\n'
         '\t    ErrorF("failed to set default font path \'%s\'", defaultFontPath);\n')
    edit(m, '\tif (!SetDefaultFont(defaultTextFont))\n'
            '\t    FatalError("could not open default font \'%s\'", defaultTextFont);\n',
         '\tif (!SetDefaultFont(defaultTextFont)) {\n'
         '\t    ErrorF("diag: SetDefaultFont %s failed\\n", defaultTextFont);\n'
         '\t    FatalError("could not open default font \'%s\'", defaultTextFont);\n'
         '\t}\n'
         '\tErrorF("diag: default font %s loaded\\n", defaultTextFont);\n')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--cpu', choices=['t425', 't800'], default='t425')
    ap.add_argument('--sysroot', type=Path, default=Path('/opt/llvm-t800/sysroots/helios'))
    ap.add_argument('--asm', default='/opt/llvm-t800/bin/asm',
                    help='the Helios assembler and linker')
    ap.add_argument('--rtlib', type=Path,
                    help='compiler-rt for Helios (default SYSROOT/lib/llvmrt-t4.lib or -t8.lib)')
    ap.add_argument('--upstream', type=Path, default=HERE.parent / 'X11R4/mit',
                    help='the X11R4 mit/ tree (kept unmodified)')
    ap.add_argument('--work', type=Path, default=HERE / 'build',
                    help='build directory: source copy and objects')
    ap.add_argument('--clang', default=os.environ.get('CLANG', '/opt/llvm-t800/bin/clang'))
    ap.add_argument('--diag', action='store_true',
                    help='diagnostic build (font loading traces) into build/<cpu>/server-diag')
    ap.add_argument('--input-test', action='store_true',
                    help='private native input injection build into server-input-test')
    args = ap.parse_args()
    upstream = args.upstream
    if not (upstream / 'server/dix/main.c').exists():
        sys.exit('Missing %s (the X11R4 mit/ tree)' % upstream)
    rtlib = args.rtlib or args.sysroot / 'lib' / ('llvmrt-t4.lib' if args.cpu == 't425' else 'llvmrt-t8.lib')
    if args.diag and args.input_test:
        ap.error('--diag and --input-test are separate variants')
    out = args.work / args.cpu / ('server-input-test' if args.input_test else
                                      'server-diag' if args.diag else 'server')
    out.mkdir(parents=True, exist_ok=True)
    mit = args.work / 'source/mit'
    # Copy only the components used by this port, preserving pristine sources.
    for component in ['server', 'include', 'extensions/server', 'extensions/include',
                      'fonts/mkfontdir', 'lib/Xau']:
        shutil.copytree(upstream / component, mit / component, dirs_exist_ok=True)
    prepare(mit)
    if args.diag:
        diag_edits(mit)
    # Recover just the keyboard translation tables, without the old polling
    # processes or their private copies of DIX structures.
    keyboard = (HERE / 'vendor/helios/devices/keyboard.c').read_text()
    keymap = keyboard.split('\nword\nDevClose(', 1)[0]
    if keymap == keyboard:
        raise RuntimeError('recovered keyboard source changed')
    end = keymap.index('typedef enum table_type')
    allocator = (HERE / 'src/keymap_alloc.inc').read_text()
    keymap = keyboard[:keyboard.index('#include')] + (
        '#include <stdlib.h>\n#include "X.h"\n#include "Xmd.h"\n#include "input.h"\n'
        '#include "keysym.h"\n#define IOdebug FatalError\n#define PROGNAME "keymap"\n'
    ) + allocator + '\n' + keymap[end:]
    # Real maps contain 121 rows starting at key 1, so the last key is 121.
    # The recovered source says 122 and makes DIX copy one row past the map.
    keymap = keymap.replace('pKeySyms->maxKeyCode = 0x7a;', 'pKeySyms->maxKeyCode = 0x79;')
    (mit / 'server/ddx/helios-keymap.c').write_text(keymap)
    inc = out / 'include'
    inc.mkdir(exist_ok=True)
    if not (inc / 'X11').exists():
        (inc / 'X11').symlink_to(mit / 'include', target_is_directory=True)
    flags = [args.clang, '--target=t800-helios', '-mcpu=' + args.cpu,
             '--sysroot=' + str(args.sysroot.resolve()), '-std=gnu89', '-Os', '-S',
             '-include', str(HERE / 'src/platform.h'), '-DUNIXCONN',
             '-DX_UNIX_PATH="X"', '-DSHAPE', '-DMULTIBUFFER', '-DMITMISC', '-DMITSHM', '-Wno-error=return-mismatch',
             '-Wno-error=implicit-function-declaration', '-Wno-error=int-conversion',
             '-Wno-error=incompatible-pointer-types', '-Wno-error=incompatible-function-pointer-types']
    dirs = ['server/include', 'include', 'extensions/include', 'server/ddx/mi', 'server/ddx/mfb',
            'server/ddx/cfb', 'server/os/4.2bsd', 'lib/Xau']
    if args.input_test:
        flags.append('-DHELIOS_INPUT_TEST')
    flags += ['-I' + str(mit / d) for d in dirs] + ['-I' + str(inc), '-I' + str(HERE / 'vendor/helios'), '-I' + str(HERE / 'src')]
    failures = []
    objects = []
    commands = []
    headers = (list(mit.rglob('*.h')) + list((HERE / 'src').glob('*.h')) +
               list((HERE / 'vendor/helios').glob('*.h')) +
               list((args.sysroot / 'include').rglob('*.h')))
    compiler_version = subprocess.check_output([args.clang, '--version'], text=True)
    header_digest = compiler_version + ''.join(hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(headers))
    for src, name, extra in sources(mit):
        obj = out / (name + '.o')
        assembly = out / (name + '.s')
        cmd = flags + extra + [str(src), '-o', str(assembly)]
        digest = hashlib.sha256((json.dumps(cmd) + src.read_text()).encode()).hexdigest()
        stamp = out / (name + '.sha256')
        # Header changes invalidate through this complete header digest.
        digest = hashlib.sha256((digest + header_digest).encode()).hexdigest()
        objects.append(str(obj))
        if obj.exists() and stamp.exists() and stamp.read_text() == digest:
            continue
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        commands.append(cmd)
        log = result.stdout
        rc = result.returncode
        if not rc:
            cmd = [args.asm, '-p', '-o', str(obj), str(assembly)]
            commands.append(cmd)
            result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
            log += result.stdout
            rc = result.returncode
        (out / (name + '.log')).write_text(log)
        if rc:
            failures.append(name)
            errors = [s for s in log.splitlines() if 'error:' in s.lower() or 'Error:' in s]
            print(name + ': ' + (errors[0] if errors else log[-300:]), flush=True)
        else:
            stamp.write_text(digest)
    (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
    (out / 'objects.json').write_text(json.dumps(objects, indent=2) + '\n')
    (out / 'failures.json').write_text(json.dumps(failures, indent=2) + '\n')
    print('%d/%d objects compiled' % (len(objects) - len(failures), len(objects)))
    if not failures:
        root = args.sysroot.resolve()
        cmd = [args.asm, '-o', str(out / 'xhelios'),
               '-nXhelios', '-s65536', '-h65536', str(root / 'lib/c0.o')] + objects
        cmd += ['-l' + str(rtlib)]
        cmd += ['-l' + str(root / 'lib' / name) for name in ['bsd.lib', 'c.lib', 'helios.lib', 'fplib.def']]
        result = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
        (out / 'link-command.json').write_text(json.dumps(cmd, indent=2) + '\n')
        (out / 'link.log').write_text(result.stdout)
        if result.returncode or 'undefined' in result.stdout.lower():
            print(result.stdout[-5000:])
            return 1
        try:
            revision = subprocess.check_output(['git', '-C', str(upstream.parent), 'rev-parse', 'HEAD'],
                                               text=True, stderr=subprocess.DEVNULL).strip()
        except (subprocess.CalledProcessError, OSError):
            revision = 'vendored'
        image = out / 'xhelios'
        (out / 'build-info.json').write_text(json.dumps({
            'upstream_commit': revision,
            'compiler': compiler_version.strip(),
            'cpu': args.cpu,
            'objects': len(objects),
            'image_bytes': image.stat().st_size,
            'image_sha256': hashlib.sha256(image.read_bytes()).hexdigest(),
            'runtime_validation': 'not performed by the build; run tests/smoke.py',
        }, indent=2) + '\n')
        print(out / 'xhelios')
    return bool(failures)


if __name__ == '__main__':
    sys.exit(main())
