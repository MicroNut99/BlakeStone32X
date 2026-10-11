#!/usr/bin/env python3
"""AUDIOHED/AUDIOT -> 32X version without the music chunks (STARTMUSIC=300 on).
usage: mkaudio.py GAMEDIR EXT OUTDIR [keep_music]"""
import sys, os, struct
gdir, ext, outdir = sys.argv[1], sys.argv[2], sys.argv[3]
keep_music = len(sys.argv) > 4
hed = open(os.path.join(gdir, 'AUDIOHED.' + ext), 'rb').read()
t = open(os.path.join(gdir, 'AUDIOT.' + ext), 'rb').read()
offs = struct.unpack('<%dI' % (len(hed) // 4), hed)
out = bytearray(); new = []
for i in range(len(offs) - 1):
    new.append(len(out))
    if i >= 300 and not keep_music:
        continue
    out += t[offs[i]:offs[i + 1]]
    while len(out) & 3:          # keep chunks 4-byte aligned for the SH2
        out.append(0)
new.append(len(out))
os.makedirs(outdir, exist_ok=True)
open(os.path.join(outdir, 'AUDIOHED.' + ext), 'wb').write(struct.pack('<%dI' % len(new), *new))
open(os.path.join(outdir, 'AUDIOT.' + ext), 'wb').write(out)
print('AUDIO %d chunks, %d -> %d bytes' % (len(new) - 1, len(t), len(out)))
