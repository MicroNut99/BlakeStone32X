#!/usr/bin/env python3
"""VGAHEAD/VGAGRAPH minus the chunks stored pre-expanded in GREXP.BS6.
usage: mkgraph.py GAMEDIR EXT GREXP OUTDIR"""
import sys, os, struct
gdir, ext, grexp, outdir = sys.argv[1:5]
head = open(os.path.join(gdir, 'VGAHEAD.' + ext), 'rb').read()
graph = open(os.path.join(gdir, 'VGAGRAPH.' + ext), 'rb').read()
gx = open(grexp, 'rb').read()
assert gx[:4] == b'GRX1'
count = struct.unpack_from('<I', gx, 4)[0]
stored = set(i for i in range(count) if struct.unpack_from('<I', gx, 8 + 4 * i)[0])
n = len(head) // 3
pos = [head[3*i] | (head[3*i+1] << 8) | (head[3*i+2] << 16) for i in range(n)]
SPARSE = 0xFFFFFF
def next_real(i):
    j = i + 1
    while pos[j] == SPARSE:
        j += 1
    return pos[j]
out = bytearray(); newpos = []; removed = 0
for i in range(n):
    if pos[i] == SPARSE:
        newpos.append(SPARSE); continue
    newpos.append(len(out))
    if i == n - 1:
        break
    data = graph[pos[i]:next_real(i)]
    if i in stored:
        removed += len(data)
        continue
    out += data
nh = bytearray()
for p in newpos:
    nh += bytes((p & 255, (p >> 8) & 255, (p >> 16) & 255))
os.makedirs(outdir, exist_ok=True)
open(os.path.join(outdir, 'VGAHEAD.' + ext), 'wb').write(nh)
open(os.path.join(outdir, 'VGAGRAPH.' + ext), 'wb').write(out)
print('GRAPH %d -> %d bytes (%d chunks now in GREXP, %d bytes removed)' % (len(graph), len(out), len(stored), removed))
