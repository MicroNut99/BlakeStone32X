#!/usr/bin/env python3
"""code.bin + data -> .32x
mkrom.py CODE DATA OFFSET OUT
  OFFSET > 4096 : data at that fixed offset (speed test)
  OFFSET = 4096 : data right after the code, on the next 4 KB boundary (game)
The result is padded to a 512 KB multiple and must fit the 4 MB 32X window."""
import sys
code = open(sys.argv[1], 'rb').read(); data = open(sys.argv[2], 'rb').read(); ofs = int(sys.argv[3])
if ofs == 4096:
    ofs = (len(code) + 4095) & ~4095
if len(code) > ofs: raise SystemExit('STOP: code is %d bytes, more than data offset %d' % (len(code), ofs))
rom = code + bytes(ofs - len(code)) + data
rom += bytes(-len(rom) % (512 * 1024))
if len(rom) > 4 * 1024 * 1024: raise SystemExit('STOP: rom is %d bytes, over 4 MB (code %d, data %d)' % (len(rom), len(code), len(data)))
open(sys.argv[4], 'wb').write(rom)
print('CODE %d DATA %d AT %d ROM %d (free %d)' % (len(code), len(data), ofs, len(rom), 4 * 1024 * 1024 - ofs - len(data)))
