#!/usr/bin/env python3
"""BlakeStone32X: the game's AdLib music for the Genesis FM chip.
Each song (AUDIOT chunk 300 on: OPL2 register writes at 700 Hz) becomes a
YM2612 VGM, played by the Genesis side (the 68000's Z80 VGM driver), packed
with the LZSS that driver reads.

The OPL2 2-operator voices are copied to YM2612 operators 1 and 2
(algorithm 4, or 7 for additive voices; operators 3 and 4 silent). The songs
use up to 8 voices at once and the YM2612 has 6, so voices are given out at
key-on: a free channel first (the one that played this voice before if it can),
else the channel that has been on the longest.

MUSIC.BS6 (little-endian): "MUS1", u32 count, then count entries of
u32 offset, u32 size, u32 length in 70 Hz ticks; the packed VGMs follow.
usage: mkmusic.py GAMEDIR EXT OUTFILE [wavdir]"""
import sys, os, struct

OPL_RATE = 49716.0
YM_CLOCK = 7670453
YM_RATE = YM_CLOCK / 144.0
MULT = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15]   # OPL MULT -> YM MUL (0 = 0.5)


def op_offsets(c):
    m = (c % 3) + 8 * (c // 3)
    return m, m + 3


class Opl:
    def __init__(self):
        self.r = [0] * 256


class Ym:
    """the YM2612 channels and what is in them"""
    def __init__(self, out):
        self.out = out
        self.owner = [None] * 6        # OPL channel
        self.keyed = [False] * 6
        self.on_time = [0] * 6
        self.off_time = [0] * 6
        self.sig = [None] * 6          # instrument last written
        self.regs = {}                 # (port, reg) -> value last written

    def w(self, port, reg, val, force=False):
        key = (port, reg)
        if not force and self.regs.get(key) == val:
            return
        self.regs[key] = val
        self.out.append(bytes((0x52 + port, reg, val)))

    def key(self, y, on):
        port, cc = divmod(y, 3)
        self.out.append(bytes((0x52, 0x28, (0xF0 if on else 0) | (port * 4 + cc))))
        self.keyed[y] = on


def ym_op_regs(opl, op, am_deep, vib_deep):
    r20, r40, r60, r80 = opl.r[0x20 + op], opl.r[0x40 + op], opl.r[0x60 + op], opl.r[0x80 + op]
    am = (r20 >> 7) & 1
    vib = (r20 >> 6) & 1
    egtyp = (r20 >> 5) & 1
    ksr = (r20 >> 4) & 1
    mul = MULT[r20 & 15]
    tl = r40 & 63
    ar = (r60 >> 4) & 15
    dr = r60 & 15
    sl = (r80 >> 4) & 15
    rr = r80 & 15
    def rate(x):
        return 31 if x == 15 else x * 2
    return {
        0x30: mul,
        0x40: tl,
        0x50: (3 if ksr else 0) << 6 | rate(ar),
        0x60: am << 7 | rate(dr),
        0x70: 0 if egtyp else rate(rr),
        0x80: sl << 4 | rr,
        0x90: 0,
    }, am, vib


def write_instrument(ym, y, opl, c):
    port, cc = divmod(y, 3)
    mod, car = op_offsets(c)
    am_deep = (opl.r[0xBD] >> 7) & 1
    vib_deep = (opl.r[0xBD] >> 6) & 1
    mregs, mam, mvib = ym_op_regs(opl, mod, am_deep, vib_deep)
    cregs, cam, cvib = ym_op_regs(opl, car, am_deep, vib_deep)
    c0 = opl.r[0xC0 + c]
    fb = (c0 >> 1) & 7
    additive = c0 & 1
    for reg, v in mregs.items():
        ym.w(port, reg + cc + 0x0, v)          # operator 1
    for reg, v in cregs.items():
        ym.w(port, reg + cc + 0x8, v)          # operator 2
    for slot in (0x4, 0xC):                    # operators 3 and 4: silent
        ym.w(port, 0x40 + cc + slot, 127)
        ym.w(port, 0x80 + cc + slot, 0x0F)
    ym.w(port, 0xB0 + cc, fb << 3 | (7 if additive else 4))
    ams = (2 if am_deep else 1) if (mam or cam) else 0
    pms = (4 if vib_deep else 2) if (mvib or cvib) else 0
    ym.w(port, 0xB4 + cc, 0xC0 | ams << 4 | pms)


def write_freq(ym, y, opl, c):
    port, cc = divmod(y, 3)
    lo = opl.r[0xA0 + c]
    hi = opl.r[0xB0 + c]
    fnum = lo | (hi & 3) << 8
    block = (hi >> 2) & 7
    # same block: YM fnum = OPL fnum * 2 * 49716 / 53267
    f = int(round(fnum * 2.0 * OPL_RATE / YM_RATE))
    while f > 2047 and block < 7:
        f >>= 1
        block += 1
    f = min(f, 2047)
    ym.w(port, 0xA4 + cc, block << 3 | f >> 8, force=True)
    ym.w(port, 0xA0 + cc, f & 255, force=True)


def convert(data):
    n = struct.unpack_from('<H', data, 0)[0]
    opl = Opl()
    cmds = []          # (wait_samples_before, bytes)
    out = []
    ym = Ym(out)
    mapping = [None] * 9
    keyed = [False] * 9
    t = 0
    steals = 0

    # set-up (before the loop point)
    init = []
    ym_init = Ym(init)
    init.append(bytes((0x52, 0x22, 0x09)))      # LFO on, 5.56 Hz
    init.append(bytes((0x52, 0x2B, 0x00)))      # DAC off
    for y in range(6):
        ym_init.key(y, False)
        port, cc = divmod(y, 3)
        for slot in (0, 4, 8, 0xC):
            init.append(bytes((0x52 + port, 0x40 + cc + slot, 127)))
            init.append(bytes((0x52 + port, 0x80 + cc + slot, 0xFF)))

    events = []        # list of (time_samples, bytes)
    for k in range(n // 4):
        reg, val, delay = struct.unpack_from('<BBH', data, 2 + 4 * k)
        before = len(out)
        old = opl.r[reg]
        opl.r[reg] = val
        if 0xB0 <= reg <= 0xB8:
            c = reg - 0xB0
            on = bool(val & 0x20)
            if on and not keyed[c]:
                # key on: find a YM channel
                y = None
                prev = mapping[c]
                if prev is not None and ym.owner[prev] == c and not ym.keyed[prev]:
                    y = prev
                if y is None:
                    free = [i for i in range(6) if not ym.keyed[i]]
                    if free:
                        y = min(free, key=lambda i: ym.off_time[i])
                if y is None:
                    y = min(range(6), key=lambda i: ym.on_time[i])
                    steals += 1
                    old_owner = ym.owner[y]
                    if old_owner is not None:
                        mapping[old_owner] = None
                    ym.key(y, False)
                ym.owner[y] = c
                mapping[c] = y
                write_instrument(ym, y, opl, c)
                write_freq(ym, y, opl, c)
                ym.key(y, True)
                ym.on_time[y] = t
            elif on and keyed[c]:
                y = mapping[c]
                if y is not None and ym.owner[y] == c:
                    write_freq(ym, y, opl, c)
            elif not on and keyed[c]:
                y = mapping[c]
                if y is not None and ym.owner[y] == c:
                    write_freq(ym, y, opl, c)
                    ym.key(y, False)
                    ym.off_time[y] = t
            else:
                y = mapping[c]
                if y is not None and ym.owner[y] == c:
                    write_freq(ym, y, opl, c)
            keyed[c] = on
        elif 0xA0 <= reg <= 0xA8:
            c = reg - 0xA0
            y = mapping[c]
            if y is not None and ym.owner[y] == c:
                write_freq(ym, y, opl, c)
        elif 0xC0 <= reg <= 0xC8 or (0x20 <= reg < 0xA0) or reg >= 0xE0 or reg == 0xBD:
            # instrument change: update the YM channels playing affected voices
            for c in range(9):
                y = mapping[c]
                if y is None or ym.owner[y] != c:
                    continue
                mod, car = op_offsets(c)
                if reg == 0xC0 + c or reg == 0xBD or (reg & 0x1F) in (mod, car) and (0x20 <= reg < 0xA0 or reg >= 0xE0):
                    write_instrument(ym, y, opl, c)
        for b in out[before:]:
            events.append((t, b))
        t += delay * 63          # 700 Hz -> 44100 Hz
    del out[:]
    for y in range(6):
        ym.key(y, False)
    for b in out:
        events.append((t, b))
    return init, events, t, steals


def vgm(init, events, total):
    body = bytearray()
    for b in init:
        body += b
    loop_at = len(body)
    now = 0
    def wait(w):
        while w > 0:
            if w <= 16:
                body.append(0x70 + w - 1)
                w = 0
            elif w == 735:
                body.append(0x62); w = 0
            elif w == 882:
                body.append(0x63); w = 0
            else:
                s = min(w, 65535)
                body.extend((0x61, s & 255, s >> 8))
                w -= s
    for tm, b in events:
        if tm > now:
            wait(tm - now)
            now = tm
        body += b
    if total > now:
        wait(total - now)
    body.append(0x66)
    hdr = bytearray(0x40)
    hdr[0:4] = b'Vgm '
    size = 0x40 + len(body)
    struct.pack_into('<I', hdr, 0x04, size - 4)
    struct.pack_into('<I', hdr, 0x08, 0x150)
    struct.pack_into('<I', hdr, 0x18, total)
    struct.pack_into('<I', hdr, 0x1C, 0x40 + loop_at - 0x1C)
    struct.pack_into('<I', hdr, 0x20, total)
    struct.pack_into('<I', hdr, 0x2C, YM_CLOCK)
    struct.pack_into('<I', hdr, 0x34, 0x40 - 0x34)
    return bytes(hdr + body)


def lzss(data):
    """the LZSS read by the Genesis side (liblzss, buffer > 4 KB): flag bits LSB
    first, 1 = copy: u16 BE (distance - 1), u8 (length - 1), 2..256; length 1 ends"""
    out = bytearray()
    i = 0
    n = len(data)
    heads = {}
    WINDOW = 0x7FFF
    flags_at = None
    bit = 8
    def flag(v):
        nonlocal flags_at, bit
        if bit == 8:
            flags_at = len(out)
            out.append(0)
            bit = 0
        if v:
            out[flags_at] |= 1 << bit
        bit += 1
    while i < n:
        best_len = 0
        best_dist = 0
        if i + 3 <= n:
            key = data[i:i + 3]
            cands = heads.get(key, [])
            for p in reversed(cands[-64:]):
                if i - p > WINDOW:
                    break
                l = 3
                mx = min(256, n - i)
                while l < mx and data[p + l] == data[i + l]:
                    l += 1
                if l > best_len:
                    best_len, best_dist = l, i - p
                    if l == mx:
                        break
        if best_len >= 4:
            flag(1)
            d = best_dist - 1
            out += bytes((d >> 8, d & 255, best_len - 1))
            for k in range(best_len):
                if i + k + 3 <= n:
                    heads.setdefault(data[i + k:i + k + 3], []).append(i + k)
            i += best_len
        else:
            flag(0)
            out.append(data[i])
            if i + 3 <= n:
                heads.setdefault(data[i:i + 3], []).append(i)
            i += 1
    flag(1)
    out += bytes((0, 0, 0))          # length 1: end
    return bytes(out)


def unlzss(src, size_hint=None):
    out = bytearray()
    i = 0
    while True:
        idb = src[i]; i += 1
        for b in range(8):
            if idb & (1 << b):
                pos = src[i] << 8 | src[i + 1]
                ln = src[i + 2] + 1
                i += 3
                if ln == 1:
                    return bytes(out)
                s = len(out) - pos - 1
                for k in range(ln):
                    out.append(out[s + k])
            else:
                out.append(src[i]); i += 1


def main():
    gdir, ext, outfile = sys.argv[1:4]
    hed = open(os.path.join(gdir, 'AUDIOHED.' + ext), 'rb').read()
    aud = open(os.path.join(gdir, 'AUDIOT.' + ext), 'rb').read()
    offs = struct.unpack('<%dI' % (len(hed) // 4), hed)
    songs = []
    for i in range(300, len(offs) - 1):
        a, b = offs[i], offs[i + 1]
        if b - a < 4:
            songs.append(None)
            continue
        init, events, total, steals = convert(aud[a:b])
        v = vgm(init, events, total)
        packed = lzss(v)
        assert unlzss(packed) == v
        songs.append((packed, total // 630))      # 44100 Hz samples -> 70 Hz ticks
        print('  song %d: %.1f s, VGM %d -> %d bytes, %d voices cut' % (i - 300, total / 44100.0, len(v), len(packed), steals))
        if len(sys.argv) > 4:
            open(os.path.join(sys.argv[4], 'song%02d.vgm' % (i - 300)), 'wb').write(v)
    out = bytearray(b'MUS1' + struct.pack('<I', len(songs)))
    table_at = len(out)
    out += bytes(12 * len(songs))
    for k, s in enumerate(songs):
        if not s:
            continue
        while len(out) & 3:
            out.append(0)
        struct.pack_into('<III', out, table_at + 12 * k, len(out), len(s[0]), s[1])
        out += s[0]
    open(outfile, 'wb').write(out)
    print('MUSIC %d songs, %d bytes' % (len(songs), len(out)))


if __name__ == '__main__':
    main()
