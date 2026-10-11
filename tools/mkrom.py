#!/usr/bin/env python3
"""code.bin + bsdata.bin -> .32x : code padded to BLOBOFS, data, padded to 512 KB."""
import sys
code = open(sys.argv[1], 'rb').read(); data = open(sys.argv[2], 'rb').read(); ofs = int(sys.argv[3])
if len(code) > ofs: raise SystemExit('STOP: code is %d bytes, more than data offset %d' % (len(code), ofs))
rom = code + bytes(ofs - len(code)) + data
rom += bytes(-len(rom) % (512 * 1024))
if len(rom) > 4 * 1024 * 1024: raise SystemExit('STOP: rom is %d bytes, over 4 MB' % len(rom))
open(sys.argv[4], 'wb').write(rom)
print('CODE %d DATA %d AT %d ROM %d' % (len(code), len(data), ofs, len(rom)))
