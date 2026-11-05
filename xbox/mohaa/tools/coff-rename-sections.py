#!/usr/bin/env python3
"""Renames sections of a COFF object in place (llvm-objcopy --rename-section
does not support COFF).

Usage: coff-rename-sections.py <obj> <old>=<new> [<old>=<new>...]

Names must fit the 8 bytes of the section header (no string table names).
"""
import struct
import sys


def main():
    path = sys.argv[1]
    renames = {}
    for arg in sys.argv[2:]:
        old, new = arg.split('=', 1)
        if len(new) > 8:
            sys.exit('section name too long: ' + new)
        renames[old.encode()] = new.encode().ljust(8, b'\0')

    data = bytearray(open(path, 'rb').read())
    num_sections = struct.unpack_from('<H', data, 2)[0]
    opt_header_size = struct.unpack_from('<H', data, 16)[0]
    table = 20 + opt_header_size

    for i in range(num_sections):
        at = table + i * 40
        name = bytes(data[at:at + 8]).rstrip(b'\0')
        if name in renames:
            data[at:at + 8] = renames[name]

    open(path, 'wb').write(data)


if __name__ == '__main__':
    main()
