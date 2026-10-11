#!/usr/bin/env python3
"""Blake Stone AOG data reader for BlakeStone32X.
Reads VSWAP.BS6, MAPHEAD.BS6, MAPTEMP.BS6 from your own game folder,
and vgapal.cpp / colormap.cpp from the BStone v0.9.0 source.
Game data is never stored in this repo."""
import struct, re, os

MAPPLANES = 2
START_TEXTURES = 125

def read_vswap(path):
    d = open(path, 'rb').read()
    n, spr, snd = struct.unpack_from('<3H', d, 0)
    offs = struct.unpack_from('<%dI' % n, d, 6)
    lens = struct.unpack_from('<%dH' % n, d, 6 + 4 * n)
    chunks = [d[o:o + l] if o else b'' for o, l in zip(offs, lens)]
    return {'count': n, 'sprite_start': spr, 'sound_start': snd, 'chunks': chunks}

def rlew_expand(words, tag, count):
    out = []; i = 0
    while len(out) < count:
        w = words[i]; i += 1
        if w == tag:
            n, v = words[i], words[i + 1]; i += 2
            out.extend([v] * n)
        else:
            out.append(w)
    return out[:count]

def read_map(head_path, temp_path, mapnum):
    h = open(head_path, 'rb').read()
    tag = struct.unpack_from('<H', h, 0)[0]
    nmaps = (len(h) - 2) // 4
    offs = struct.unpack_from('<%di' % nmaps, h, 2)
    t = open(temp_path, 'rb').read()
    pos = offs[mapnum]
    if pos <= 0:
        raise ValueError('map %d missing' % mapnum)
    pstart = struct.unpack_from('<3i', t, pos)
    plen = struct.unpack_from('<3H', t, pos + 12)
    w, hh = struct.unpack_from('<2H', t, pos + 18)
    name = t[pos + 22:pos + 38].split(b'\0')[0].decode('latin-1')
    planes = []
    for p in range(MAPPLANES):
        raw = t[pstart[p]:pstart[p] + plen[p]]
        words = struct.unpack('<%dH' % (len(raw) // 2), raw)
        planes.append(rlew_expand(words[1:], tag, w * hh))  # skip expanded length
    return {'name': name, 'width': w, 'height': hh, 'planes': planes, 'count': nmaps}

def read_c_bytes(path):
    s = open(path, 'r', encoding='latin-1').read()
    body = s[s.index('{', s.index('= {') if '= {' in s else s.index('{')) + 1:]
    body = body[:body.index('}')]
    return bytes(int(x, 0) for x in re.findall(r'0x[0-9A-Fa-f]+|\b\d+\b', body))
