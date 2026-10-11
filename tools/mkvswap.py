#!/usr/bin/env python3
"""VSWAP for the cart: digitized sound pages stored as 4-bit IMA ADPCM.
Each sound page (up to 4096 unsigned 8-bit samples) becomes:
  u16 sample count (LE), s16 first predictor (LE), u8 step index, u8 pad, nibbles (low first).
Walls, sprites and the last chunk (the digitized sound list) are unchanged.
usage: mkvswap.py GAMEDIR EXT OUTDIR"""
import sys, os, struct

STEPS = [7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,
 118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,
 1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,
 5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,
 27086,29794,32767]
IDX = [-1,-1,-1,-1,2,4,6,8,-1,-1,-1,-1,2,4,6,8]

def adpcm_encode(samples8):
    pcm = [(s - 128) << 8 for s in samples8]
    pred = pcm[0] if pcm else 0
    idx = 0
    out = bytearray(struct.pack('<Hhbb', len(pcm), pred, 0, 0))
    nib = []
    for s in pcm[1:]:
        step = STEPS[idx]
        diff = s - pred
        code = 0
        if diff < 0:
            code = 8; diff = -diff
        delta = step >> 3
        if diff >= step: code |= 4; diff -= step; delta += step
        if diff >= step >> 1: code |= 2; diff -= step >> 1; delta += step >> 1
        if diff >= step >> 2: code |= 1; delta += step >> 2
        pred = pred - delta if code & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IDX[code]))
        nib.append(code)
    if len(nib) & 1:
        nib.append(0)
    for i in range(0, len(nib), 2):
        out.append(nib[i] | (nib[i + 1] << 4))
    return bytes(out)

def adpcm_decode(data):
    n, pred, idx, _ = struct.unpack_from('<Hhbb', data, 0)
    out = [pred]
    for i in range(n - 1):
        b = data[6 + (i >> 1)]
        code = (b >> 4) if i & 1 else (b & 15)
        step = STEPS[idx]
        delta = step >> 3
        if code & 4: delta += step
        if code & 2: delta += step >> 1
        if code & 1: delta += step >> 2
        pred = pred - delta if code & 8 else pred + delta
        pred = max(-32768, min(32767, pred))
        idx = max(0, min(88, idx + IDX[code]))
        out.append(pred)
    return out

def main():
    gdir, ext, outdir = sys.argv[1:4]
    d = open(os.path.join(gdir, 'VSWAP.' + ext), 'rb').read()
    n, spr, snd = struct.unpack_from('<3H', d, 0)
    offs = struct.unpack_from('<%dI' % n, d, 6)
    lens = struct.unpack_from('<%dH' % n, d, 6 + 4 * n)
    chunks = [d[o:o + l] if o else b'' for o, l in zip(offs, lens)]
    before = sum(len(c) for c in chunks[snd:n - 1])
    for i in range(snd, n - 1):
        if chunks[i]:
            chunks[i] = adpcm_encode(chunks[i])
    after = sum(len(c) for c in chunks[snd:n - 1])
    hdr = 6 + 6 * n
    pos = (hdr + 15) & ~15
    newoffs = []; body = bytearray()
    for c in chunks:
        if not c:
            newoffs.append(0); continue
        while (pos + len(body)) & 3:
            body.append(0)
        newoffs.append(pos + len(body))
        body += c
    out = bytearray(struct.pack('<3H', n, spr, snd))
    out += struct.pack('<%dI' % n, *newoffs)
    out += struct.pack('<%dH' % n, *[len(c) for c in chunks])
    out += b'\0' * (pos - len(out))
    out += body
    os.makedirs(outdir, exist_ok=True)
    open(os.path.join(outdir, 'VSWAP.' + ext), 'wb').write(out)
    print('VSWAP %d -> %d bytes (sounds %d -> %d)' % (len(d), len(out), before, after))

if __name__ == '__main__':
    main()
