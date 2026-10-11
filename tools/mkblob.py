#!/usr/bin/env python3
"""Build bsdata.bin (big-endian blob for BlakeStone32X) from your own AOG files.
usage: mkblob.py GAMEDIR BSTONE_SRC_DIR MAPNUM OUT.bin"""
import sys, struct, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bsdata

def nearest(pal, rgb):
    best = None
    for i in range(256):
        r, g, b = pal[i*3:i*3+3]
        d = (r-rgb[0])**2 + (g-rgb[1])**2 + (b-rgb[2])**2
        if best is None or d < best[0]: best = (d, i)
    return best[1]

def main():
    gdir, sdir, mapnum, out = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
    vs = bsdata.read_vswap(os.path.join(gdir, 'VSWAP.BS6'))
    m = bsdata.read_map(os.path.join(gdir, 'MAPHEAD.BS6'), os.path.join(gdir, 'MAPTEMP.BS6'), mapnum)
    pal6 = bsdata.read_c_bytes(os.path.join(sdir, 'vgapal.cpp'))
    cmap = bsdata.read_c_bytes(os.path.join(sdir, 'colormap.cpp'))
    assert len(pal6) == 768 and len(cmap) >= 64 * 256, (len(pal6), len(cmap))
    pal = bytes((c << 2) | (c >> 4) for c in pal6)
    p0, p1 = m['planes']
    nw = vs['sprite_start']

    start = None; ceil = floor = 126; gottex = False
    for i, w in enumerate(p1):
        if w in (19, 20, 21, 22) and start is None:
            start = (i % 64, i // 64, w - 19)
        if (w & 0xff00) == 0xfb00 and i + 1 < len(p1) and not gottex:
            t = p1[i + 1]; gottex = True
            ceil = bsdata.START_TEXTURES + (t >> 8); floor = bsdata.START_TEXTURES + (t & 0xff)
    if start is None: raise SystemExit('STOP: no player start in map %d' % mapnum)
    ang = {0: 256, 1: 0, 2: 768, 3: 512}[start[2]]   # NORTH, EAST, SOUTH, WEST

    tm = bytearray(64 * 64)
    for i, t in enumerate(p0):
        tm[i] = t if 0 < t < 64 else 0      # walls only; doors (88-105) and areas open for the speed test

    def avg(pic):
        c = vs['chunks'][pic]; r = g = b = 0
        for p in c: r += pal[p*3]; g += pal[p*3+1]; b += pal[p*3+2]
        n = len(c); return (r//n, g//n, b//n)

    HDR = 4 * 20 + 16
    body = bytearray()
    def put(data):
        while (HDR + len(body)) % 16: body.append(0)
        o = HDR + len(body); body.extend(data); return o
    o_pal = put(pal); o_cm = put(cmap[:64*256]); o_tm = put(tm)
    o_dir = put(bytes(4 * nw))
    dirv = []
    for i in range(nw):
        c = vs['chunks'][i]
        dirv.append(put(c) if len(c) == 4096 else 0)
    d0 = o_dir - HDR
    body[d0:d0 + 4*nw] = struct.pack('>%dI' % nw, *dirv)

    size = HDR + len(body)
    hdr = struct.pack('>20I', 0x42533332, 1, nw, mapnum, start[0], start[1], ang, ceil, floor,
                      [1, 6, 3, 4, 1, 2][mapnum % 6], [36, 51, 62, 63, 18, 52][mapnum % 6],
                      o_pal, o_cm, o_tm, o_dir, size,
                      nearest(pal, (255, 255, 255)), nearest(pal, (0, 0, 0)),
                      nearest(pal, avg(ceil)), nearest(pal, avg(floor)))
    hdr += m['name'].encode('latin-1')[:15].ljust(16, b'\0')
    assert len(hdr) == HDR
    open(out, 'wb').write(hdr + body)
    print('MAP %d %s START %d %d ANG %d CEIL %d FLOOR %d WALLS %d SIZE %d' %
          (mapnum, m['name'], start[0], start[1], ang, ceil, floor, nw, size))

main()
