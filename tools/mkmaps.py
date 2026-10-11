#!/usr/bin/env python3
"""MAPHEAD/MAPTEMP -> BlakeStone32X maps: same header layout, each plane stored
expanded (64x64 little-endian words) and LZ4-compressed. MAPHEAD's RLEW tag is
set to 0x4C5A ('LZ') so CA_CacheMap uses the LZ path.
usage: mkmaps.py GAMEDIR EXT OUTDIR"""
import sys, os, struct
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lz4c

def rlew_expand(data, tag, count):
    words = struct.unpack('<%dH' % (len(data) // 2), data)
    out = []; i = 1                      # skip expanded length word
    while len(out) < count:
        w = words[i]; i += 1
        if w == tag:
            n, v = words[i], words[i + 1]; i += 2
            out.extend([v] * n)
        else:
            out.append(w)
    return out[:count]

def main():
    gdir, ext, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
    head = open(os.path.join(gdir, 'MAPHEAD.' + ext), 'rb').read()
    temp = open(os.path.join(gdir, 'MAPTEMP.' + ext), 'rb').read()
    tag = struct.unpack_from('<H', head, 0)[0]
    nmaps = (len(head) - 2) // 4
    offs = struct.unpack_from('<%di' % nmaps, head, 2)
    out = bytearray(b'LZ32')            # file magic; headers follow
    newoffs = []
    raw = comp = 0
    for m in range(nmaps):
        pos = offs[m]
        if pos <= 0:
            newoffs.append(pos); continue
        pstart = struct.unpack_from('<3i', temp, pos)
        plen = struct.unpack_from('<3H', temp, pos + 12)
        w, h = struct.unpack_from('<2H', temp, pos + 18)
        name = temp[pos + 22:pos + 38]
        planes = []
        for p in range(2):
            try:
                words = rlew_expand(temp[pstart[p]:pstart[p] + plen[p]], tag, w * h)
                data = struct.pack('<%dH' % (w * h), *words)
                planes.append(lz4c.compress(data))
                raw += len(data)
            except Exception:
                planes.append(b'')
        hdr_pos = len(out)
        newoffs.append(hdr_pos)
        p0 = hdr_pos + 38
        p1 = p0 + len(planes[0])
        out += struct.pack('<3i3H2H', p0, p1, 0, len(planes[0]), len(planes[1]), 0, w, h) + name
        out += planes[0] + planes[1]
        comp += len(planes[0]) + len(planes[1])
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, 'MAPHEAD.' + ext), 'wb').write(struct.pack('<H', 0x4C5A) + struct.pack('<%di' % nmaps, *newoffs))
    open(os.path.join(outdir, 'MAPTEMP.' + ext), 'wb').write(out)
    print('MAPS %d planes raw %d lz4 %d file %d (was %d)' % (len([o for o in newoffs if o > 0]), raw, comp, len(out), len(temp)))

main()
