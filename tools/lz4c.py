#!/usr/bin/env python3
"""Small LZ4 block-format compressor (no frame, no size header) for BlakeStone32X.
Decoder: port/lz4d.c. Greedy match search with hash chains."""

MINMATCH = 4
MAXDIST = 65535

def compress(src, depth=64):
    n = len(src)
    out = bytearray()
    head = {}
    prev = [-1] * n
    anchor = 0
    i = 0
    last_lits = n - 5          # LZ4 rule: last 5 bytes are literals
    mflimit = n - 12           # no match may start in the last 12 bytes

    def put_len(v):
        while v >= 255:
            out.append(255); v -= 255
        out.append(v)

    def emit(lit_end, mlen, dist):
        lits = lit_end - anchor
        tok = (min(lits, 15) << 4) | (min(mlen - MINMATCH, 15) if mlen else 0)
        out.append(tok)
        if lits >= 15:
            put_len(lits - 15)
        out.extend(src[anchor:lit_end])
        if mlen:
            out.append(dist & 255); out.append(dist >> 8)
            if mlen - MINMATCH >= 15:
                put_len(mlen - MINMATCH - 15)

    def insert(p):
        if p + 4 <= n:
            k = src[p:p + 4]
            prev[p] = head.get(k, -1)
            head[k] = p

    while i < mflimit:
        k = src[i:i + 4]
        cand = head.get(k, -1)
        best_len = 0; best_pos = -1; d = 0
        while cand >= 0 and i - cand <= MAXDIST and d < depth:
            l = 0
            maxl = last_lits - i
            while l < maxl and src[cand + l] == src[i + l]:
                l += 1
            if l > best_len:
                best_len = l; best_pos = cand
            cand = prev[cand]; d += 1
        if best_len >= MINMATCH:
            emit(i, best_len, i - best_pos)
            for p in range(i, i + best_len):
                insert(p)
            i += best_len
            anchor = i
        else:
            insert(i)
            i += 1
    # final literals
    lits = n - anchor
    out.append(min(lits, 15) << 4)
    if lits >= 15:
        put_len(lits - 15)
    out.extend(src[anchor:])
    return bytes(out)

def decompress(src, size):
    out = bytearray(); i = 0
    while i < len(src):
        tok = src[i]; i += 1
        l = tok >> 4
        if l == 15:
            while True:
                b = src[i]; i += 1; l += b
                if b != 255: break
        out.extend(src[i:i + l]); i += l
        if i >= len(src): break
        d = src[i] | (src[i + 1] << 8); i += 2
        m = tok & 15
        if m == 15:
            while True:
                b = src[i]; i += 1; m += b
                if b != 255: break
        m += MINMATCH
        for _ in range(m):
            out.append(out[-d])
    assert len(out) == size, (len(out), size)
    return bytes(out)

if __name__ == '__main__':
    import sys, os
    d = open(sys.argv[1], 'rb').read()
    c = compress(d)
    assert decompress(c, len(d)) == d
    print(len(d), '->', len(c))
