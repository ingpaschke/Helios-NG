#!/usr/bin/env python3
"""Copy the Helios-NG working tree into a build tree (build.sh).

    headco.py REPO OUTDIR [--manifest FILE] [--products FILE]

Copies every working file except RCS/ directories, .git, tools/ and the
build directory (OUTDIR, or build-t800/ inside the checkout).  Inside
processor directories (TRAN, SUN4, C40, ...) a file is copied only when it
looks like source or has an RCS file; everything else there is a 1994 build
product of Perihelion's own build and is listed in --products instead.
Nothing in REPO is modified.
"""
import argparse
import os
import re
import shutil

# processor and host directories, and what counts as source inside them
PROC_DIRS = {'TRAN', 'HELIOSTRAN', 'SUN4', 'SUN3', 'SUN386', 'RS6000', 'HP',
             'R140', 'C40', 'HELIOSC40', 'ARM', 'HELIOSARM', 'M68K', '68K',
             'I860', 'SCOUNIX'}
# the time stamp of the copied files (SOURCE_DATE_EPOCH, else 2000-01-01)
MTIME = int(os.environ.get('SOURCE_DATE_EPOCH', '946684800'))
SRC_RE = re.compile(r'(\.(c|h|a|m|inc|y|l|mak|cdl|con)$)|(^[Mm]akefile)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('repo')
    ap.add_argument('out')
    ap.add_argument('--manifest')
    ap.add_argument('--products')
    a = ap.parse_args()
    repo = os.path.abspath(a.repo)
    out = os.path.abspath(a.out)
    man, prods = [], []
    for root, ds, fs in os.walk(repo):
        rel = os.path.relpath(root, repo)
        # not tools/, .git, nor a build directory inside the checkout
        if rel.split(os.sep)[0] in ('tools', '.git', 'build-t800') or \
           os.path.commonpath([os.path.abspath(root), out]) == out:
            ds[:] = []
            continue
        rcs = set()
        if 'RCS' in ds:
            rcs = {f[:-2] for f in os.listdir(os.path.join(root, 'RCS'))
                   if f.endswith(',v')}
        rcs |= {f[:-2] for f in fs if f.endswith(',v')}
        ds[:] = [d for d in ds if d != 'RCS']
        inproc = any(p in PROC_DIRS for p in rel.split(os.sep))
        for f in fs:
            if f.endswith(',v'):
                continue
            wpath = os.path.normpath(os.path.join(rel, f))
            if inproc and not SRC_RE.search(f) and f not in rcs:
                prods.append(wpath)
                continue
            if inproc and f.endswith('.o') :
                prods.append(wpath)
                continue
            dst = os.path.join(a.out, wpath)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            if os.path.lexists(dst):
                os.remove(dst)
            shutil.copy2(os.path.join(repo, wpath), dst, follow_symlinks=False)
            # one time stamp for every source file: a git checkout sets
            # them in checkout order, which would make rules such as
            # tcpip/nfs "mount.h: mount.x" (rpcgen) or b422's squirtdata.c
            # look out of date at random
            os.utime(dst, (MTIME, MTIME), follow_symlinks=False)
            man.append(('worktree' if f not in rcs else 'worktree-rcs', wpath))
    if a.manifest:
        with open(a.manifest, 'w') as fh:
            for how, p in sorted(man, key=lambda x: x[1]):
                fh.write('%s\t%s\n' % (how, p))
    if a.products:
        with open(a.products, 'w') as fh:
            for p in sorted(prods):
                fh.write(p + '\n')
    print('copied', len(man), 'products skipped', len(prods))


if __name__ == '__main__':
    main()
