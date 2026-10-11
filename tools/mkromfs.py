#!/usr/bin/env python3
"""Pack game files into the BlakeStone32X cart file table.
Layout (big-endian): 'BSFS32X!' count(u32) pad(u32), then count entries of
name[16] offset(u32, from table start) size(u32); files follow, 16-byte aligned.
usage: mkromfs.py OUT FILE..."""
import sys, os, struct
out, files = sys.argv[1], sys.argv[2:]
hdr = b'BSFS32X!' + struct.pack('>II', len(files), 0)
pos = len(hdr) + 24 * len(files)
pos = (pos + 15) & ~15
table = b''; blob = b''
for f in files:
    data = open(f, 'rb').read()
    name = os.path.basename(f).upper().encode()
    assert len(name) < 16, name
    table += name.ljust(16, b'\0') + struct.pack('>II', pos + len(blob), len(data))
    blob += data
    while len(blob) & 15:
        blob += b'\0'
    print('  %-14s %8d' % (name.decode(), len(data)))
img = hdr + table
img += b'\0' * (((len(img) + 15) & ~15) - len(img))
img += blob
open(out, 'wb').write(img)
print('ROMFS %d files, %d bytes' % (len(files), len(img)))
