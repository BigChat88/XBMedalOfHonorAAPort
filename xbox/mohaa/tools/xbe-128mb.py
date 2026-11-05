"""Clears the "Limit64MB" flag in an XBE header.

cxbe always sets it. With the flag set, the kernel only gives the game 64 MB
even if the console (or xemu) has 128 MB. Without it, the game uses all
available RAM and still works on 64 MB consoles.

Usage: python3 xbe-128mb.py default.xbe
"""
import sys

INIT_FLAGS_OFFSET = 0x124
LIMIT_64MB = 0x04  # bit 2: bMountUtilityDrive, bFormatUtilityDrive, bLimit64MB, ...

path = sys.argv[1]
with open(path, 'r+b') as f:
    if f.read(4) != b'XBEH':
        sys.exit(f'{path}: not an XBE')
    f.seek(INIT_FLAGS_OFFSET)
    flags = f.read(1)[0]
    f.seek(INIT_FLAGS_OFFSET)
    f.write(bytes([flags & ~LIMIT_64MB]))
print(f'{path}: InitFlags 0x{flags:02x} -> 0x{flags & ~LIMIT_64MB:02x}')
