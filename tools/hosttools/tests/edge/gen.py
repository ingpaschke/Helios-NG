#!/usr/bin/env python3
# gen.py DIR -- write the binary edge-case inputs of edge.sh into DIR:
# image parts for sysbuild and image files for objed (little-endian words).
import os, struct, sys

d = sys.argv[1]
w = lambda *v: struct.pack("<%dI" % len(v), *[x & 0xffffffff for x in v])

def put(name, data):
    with open(os.path.join(d, name), "wb") as f:
        f.write(data)

MAGIC = 0x12345678
T_Program, T_Module, T_ResRef = 0x60f060f0, 0x60f160f1, 0x60f260f2

# ---- sysbuild parts: 12-byte header {Magic, Flags, Size} + Size bytes
put("sb-ok.i", w(MAGIC, 0, 5) + b"abcde")               # odd size: padding
put("sb-ok8.i", w(MAGIC, 0, 8) + b"ABCDEFGH")
put("sb-short.i", w(MAGIC, 0, 50) + b"abc")             # file shorter than size
put("sb-neg.i", w(MAGIC, 0, -16) + bytes(64))           # negative size
put("sb-big.i", w(MAGIC, 0, 0x7ffffff0) + bytes(1200000))   # > 1 MB
put("sb-ovf.i", w(MAGIC, 0, 0x7ffffffe) + bytes(1100000))   # isize+size+3 wraps

# ---- objed images: header + modules
def module(mtype, size, name, mid=1, ver=1, maxdata=0, init=0):
    n = name.encode()[:32].ljust(32, b"\0")
    return w(mtype, size) + n + w(mid, ver, maxdata, init)

def program(size, name, stack=1000, heap=2000):
    return module(T_Program, size, name) + w(stack, heap, 0)   # 68 bytes

def image(body, size=None):
    return w(MAGIC, 0, len(body) if size is None else size) + body

# a good image: program, module, resref, terminator
good = program(68, "prog") + module(T_Module, 56, "mod", 2) + \
       module(T_ResRef, 56, "ref", 3) + w(0)
put("ob-good", image(good))
# a module of size 0: looped for ever
put("ob-zerosize", image(program(68, "zero") + module(T_Module, 0, "zero") + w(0) + bytes(128)))
# no terminating 0 word: the chain runs off the image
put("ob-noterm", image(program(68, "noterm")))
# a module size past the end of the image
put("ob-pastend", image(program(1000, "pastend") + w(0)))
# a negative image size in the header
put("ob-negsize", image(program(68, "p", 1, 2) + w(0) + bytes(8), size=-8))
# an image name of 32 characters without a NUL
put("ob-longname", image(program(68, "N" * 32) + module(T_Module, 56, "M" * 32) + w(0)))
# truncated header
put("ob-short", w(MAGIC, 0, 100))
