"""Resolves XBE virtual addresses (e.g. from a bugcheck) with the lld map.

Usage: python3 tools/addr2sym.py 0x000EC3E8 [more addresses] [--map build/main.map]
"""
import argparse
import bisect
import re

parser = argparse.ArgumentParser()
parser.add_argument('addresses', nargs='+')
parser.add_argument('--map', default='build/main.map')
args = parser.parse_args()

# "Publics by Value" lines: " 0001:00000000       _Name     0000000000011000     obj"
line_re = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+(.*)$')
symbols = []
with open(args.map, encoding='utf-8', errors='replace') as f:
    for line in f:
        m = line_re.match(line)
        if m:
            symbols.append((int(m.group(2), 16), m.group(1), m.group(3).strip()))
symbols.sort()
keys = [s[0] for s in symbols]

for text in args.addresses:
    addr = int(text, 16)
    i = bisect.bisect_right(keys, addr) - 1
    if i < 0:
        print(f'{addr:#010x}  ?')
        continue
    va, name, obj = symbols[i]
    print(f'{addr:#010x}  {name} + {addr - va:#x}  ({obj})')
