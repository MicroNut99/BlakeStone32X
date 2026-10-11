import struct, random, sys
M32 = 0xFFFFFFFF
def sx8(v): return v - 256 if v & 0x80 else v
def sx16(v): return v - 65536 if v & 0x8000 else v
class CPU:
    def __init__(s):
        s.r = [0]*16; s.mem = {}; s.T = 0; s.pc = 0; s.pr = 0; s.writes = []
    def rb(s, a): return s.mem[a & M32]
    def wb(s, a, v): s.mem[a & M32] = v & 0xFF
    def rw(s, a): return (s.rb(a) << 8) | s.rb(a+1)
    def rl(s, a): return (s.rw(a) << 16) | s.rw(a+2)
    def ww(s, a, v): s.wb(a, v >> 8); s.wb(a+1, v)
    def wl(s, a, v): s.ww(a, v >> 16); s.ww(a+2, v)
    def load(s, a, data):
        for i, b in enumerate(data): s.mem[a+i] = b
    def step(s, pc):
        """execute instruction at pc, return (next_pc, delayed_branch_target or None)"""
        op = s.rw(pc); r = s.r; n = (op >> 8) & 15; m = (op >> 4) & 15
        hi = op >> 12; lo = op & 15
        nxt = pc + 2
        if hi == 2:
            if lo == 6: r[n] = (r[n]-4) & M32; s.wl(r[n], r[m])
            elif lo == 5: r[n] = (r[n]-2) & M32; s.ww(r[n], r[m] & 0xFFFF); s.writes.append(('w', r[n], r[m] & 0xFFFF))
            elif lo == 0: s.wb(r[n], r[m]); s.writes.append(('b', r[n], r[m] & 0xFF))
            elif lo == 1: s.ww(r[n], r[m] & 0xFFFF); s.writes.append(('w', r[n], r[m] & 0xFFFF))
            elif lo == 9: r[n] &= r[m]
            elif lo == 11: r[n] |= r[m]
            elif lo == 8: s.T = int((r[n] & r[m]) == 0)
            else: raise ValueError(hex(op))
        elif hi == 5: r[n] = s.rl(r[m] + lo*4)
        elif hi == 6:
            if lo == 3: r[n] = r[m]
            elif lo == 12: r[n] = r[m] & 0xFF
            elif lo == 6: r[n] = s.rl(r[m]); 
            else: raise ValueError(hex(op))
            if lo == 6 and n != m: r[m] = (r[m]+4) & M32
        elif hi == 7: r[n] = (r[n] + sx8(op & 0xFF)) & M32
        elif hi == 3 and lo == 12: r[n] = (r[n] + r[m]) & M32
        elif hi == 9: r[n] = sx16(s.rw(pc + 4 + (op & 0xFF)*2)) & M32
        elif hi == 4:
            k = op & 0xFF
            if k == 0x29: r[n] >>= 16
            elif k == 0x19: r[n] >>= 8
            elif k == 0x09: r[n] >>= 2
            elif k == 0x01: s.T = r[n] & 1; r[n] >>= 1
            elif k == 0x18: r[n] = (r[n] << 8) & M32
            elif k == 0x10: r[n] = (r[n]-1) & M32; s.T = int(r[n] == 0)
            else: raise ValueError(hex(op))
        elif hi == 0:
            if op == 0x000B: return nxt, s.pr
            if lo == 12: r[n] = sx8(s.rb(r[0] + r[m])) & M32
            elif lo == 13: r[n] = sx16(s.rw((r[0] + r[m]) & M32)) & M32
            else: raise ValueError(hex(op))
        elif hi == 0xC:
            k = (op >> 8) & 15
            if k == 9: r[0] &= op & 0xFF
            elif k == 8: s.T = int((r[0] & (op & 0xFF)) == 0)
            else: raise ValueError(hex(op))
        elif hi == 8:
            k = (op >> 8) & 15; tgt = pc + 4 + sx8(op & 0xFF)*2
            if k == 0xB: return (tgt if not s.T else nxt), None          # bf
            if k == 0x9: return (tgt if s.T else nxt), None              # bt
            if k == 0xF: return nxt, (tgt if not s.T else nxt + 2)       # bf/s
            if k == 0xD: return nxt, (tgt if s.T else nxt + 2)           # bt/s
            raise ValueError(hex(op))
        else: raise ValueError(hex(op))
        return nxt, None
    def call(s, entry, args, stackargs):
        r = s.r
        for i, a in enumerate(args): r[4+i] = a & M32
        sp = 0x0603F000 - 4*len(stackargs)
        for i, a in enumerate(stackargs): s.wl(sp + 4*i, a & M32)
        r[15] = sp; s.pr = 0xDEAD0000
        saved = r[8:15]
        pc = entry; n = 0
        while pc != 0xDEAD0000:
            pc2, delayed = s.step(pc)
            if delayed is not None:
                pc3, d2 = s.step(pc2); assert d2 is None, 'branch in delay slot'
                pc = delayed
            else: pc = pc2
            n += 1
            if n > 10**7: raise RuntimeError('runaway')
        assert r[8:15] == saved, 'callee-saved regs changed'
        assert r[15] == sp, 'stack not restored'
        return n
