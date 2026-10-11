#!/usr/bin/env python3
"""Check the SH2 loops in src/r_asm.s against reference models, using sh2sim.py.
usage: test_asm.py r_asm.o path/to/sh-elf-  (toolchain prefix for readelf/nm)"""
import random, sys, subprocess, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sh2sim import CPU
obj, prefix = sys.argv[1], sys.argv[2]
d = open(obj, 'rb').read()
for l in subprocess.run([prefix + 'readelf', '-S', obj], capture_output=True, text=True).stdout.splitlines():
    if '.sdata' in l:
        f = l.split(); i = f.index('PROGBITS'); off = int(f[i + 2], 16); size = int(f[i + 3], 16)
code = d[off:off + size]
syms = {}
for l in subprocess.run([prefix + 'nm', obj], capture_output=True, text=True).stdout.splitlines():
    a, _, n = l.split(); syms[n] = int(a, 16)
BASE = 0x06000000; M32 = 0xFFFFFFFF
def tidx(xy): return ((xy >> 3) & 0x1F80) | ((xy >> 25) & 0x7E)
def sxb(v): return v - 256 if v & 0x80 else v
random.seed(2)
pp = bytes(random.randrange(256) for _ in range(8192))
sh16 = [random.randrange(1, 65536) for _ in range(256)]
sh8 = bytes(random.randrange(1, 256) for _ in range(256))
PP = 0x06010000; SH = 0x06020000; SH8 = 0x06030000; SRC = 0x06021000; FB = 0x24000000
def mk():
    c = CPU(); c.load(BASE, code); c.load(PP, pp); c.load(SH8, sh8)
    for i, v in enumerate(sh16): c.mem[SH + 2 * i] = v >> 8; c.mem[SH + 2 * i + 1] = v & 255
    for a in range(0x0603E000, 0x0603F100): c.mem[a] = 0
    return c
def w16(c, a): return (c.mem.get(a), c.mem.get(a + 1))
fail = 0
# R_SpanPairsAsm
bad = tests = 0
for t in range(200):
    c = mk(); pairs = random.randint(1, 40); xy = random.getrandbits(32); st = random.getrandbits(32)
    c.call(BASE + syms['_R_SpanPairsAsm'], [FB + 0x1000 + 2 * pairs, FB + 0x8000 + 2 * pairs, pairs, (xy + (2 * pairs - 1) * st) & M32], [(-st) & M32, PP, SH8 + 128])
    x = xy
    for p in range(2 * pairs):
        i = tidx(x); x = (x + st) & M32; tests += 2
        bad += c.mem.get(FB + 0x1000 + p) != sh8[128 + sxb(pp[i])]
        bad += c.mem.get(FB + 0x8000 + p) != sh8[128 + sxb(pp[i + 1])]
    if len(c.writes) != 2 * pairs: bad += 1
print('R_SpanPairsAsm pixels', tests, 'bad', bad); fail += bad
# R_ColAsm
bad = tests = 0
for t in range(200):
    c = mk(); src = bytes(random.randrange(256) for _ in range(64)); c.load(SRC, src)
    n = random.randint(1, 200); frac = random.randrange(0, 64 << 16); step = random.randrange(1, 4 << 16)
    c.call(BASE + syms['_R_ColAsm'], [FB + 50, n, frac, step], [SRC, SH8 + 128, 320]); f = frac
    for k in range(n):
        tests += 1; bad += c.mem.get(FB + 50 + k * 320) != sh8[128 + sxb(src[(f >> 16) & 63])]; f += step
    if len(c.writes) != n: bad += 1
print('R_ColAsm pixels', tests, 'bad', bad); fail += bad
# R_SpanLoAsm
bad = tests = 0
for t in range(200):
    c = mk(); n = random.randint(1, 60); xy = random.getrandbits(32); st = random.getrandbits(32)
    c.call(BASE + syms['_R_SpanLoAsm'], [FB + 0x1000 + 2 * n, FB + 0x8000 + 2 * n, n, (xy + (n - 1) * st) & M32], [(-st) & M32, PP, SH + 256])
    x = xy
    for p in range(n):
        i = tidx(x); x = (x + st) & M32
        for base, v in ((FB + 0x1000, pp[i]), (FB + 0x8000, pp[i + 1])):
            e = sh16[128 + sxb(v)]; tests += 1; bad += w16(c, base + 2 * p) != (e >> 8, e & 255)
    if len(c.writes) != 2 * n: bad += 1
print('R_SpanLoAsm words', tests, 'bad', bad); fail += bad
# R_ColAsm16
bad = tests = 0
for t in range(200):
    c = mk(); src = bytes(random.randrange(256) for _ in range(64)); c.load(SRC, src)
    n = random.randint(1, 200); frac = random.randrange(0, 64 << 16); step = random.randrange(1, 4 << 16)
    c.call(BASE + syms['_R_ColAsm16'], [FB + 50, n, frac, step], [SRC, SH + 256, 320]); f = frac
    for k in range(n):
        e = sh16[128 + sxb(src[(f >> 16) & 63])]; tests += 1
        bad += w16(c, FB + 50 + k * 320) != (e >> 8, e & 255); f += step
    if len(c.writes) != n: bad += 1
print('R_ColAsm16 words', tests, 'bad', bad); fail += bad
print('ALL OK' if fail == 0 else 'FAILED')
sys.exit(1 if fail else 0)
