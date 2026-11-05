#!/usr/bin/env python3
"""Lists the biggest symbols of the data/BSS sections of the XBE, from the
lld-link map (build/main.map).

Usage: python3 tools/bss-top.py [build/main.map] [count]
A symbol's size is the distance to the next symbol of the same section, so the
last symbol of each section is left out and padding counts as part of the
symbol before it.
"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'build/main.map'
count = int(sys.argv[2]) if len(sys.argv) > 2 else 40

sections = {}   # index -> name
symbols = {}    # index -> [(offset, name, object)]
in_publics = False
for line in open(path, encoding='latin1'):
    m = re.match(r'\s*([0-9a-f]{4}):([0-9a-f]{8})\s+([0-9a-f]+)H\s+(\S+)\s+(CODE|DATA)', line)
    if m and not in_publics:
        idx = int(m.group(1), 16)
        sections.setdefault(idx, m.group(4).split('$')[0])
        continue
    if 'Publics by Value' in line:
        in_publics = True
        continue
    m = re.match(r'\s*([0-9a-f]{4}):([0-9a-f]{8})\s+(\S+)\s+[0-9a-f]+\s+(.*)$', line)
    if m and in_publics:
        symbols.setdefault(int(m.group(1), 16), []).append((int(m.group(2), 16), m.group(3), m.group(4).strip()))

rows = []
for idx, syms in symbols.items():
    name = sections.get(idx, '?')
    if not any(k in name for k in ('bss', 'data')):
        continue
    syms.sort()
    for (off, sym, obj), (nxt, _, _) in zip(syms, syms[1:]):
        rows.append((nxt - off, name, sym, obj))

rows.sort(reverse=True)
total = sum(r[0] for r in rows)
print(f'{total // 1024} KB in data/BSS symbols')
for size, sec, sym, obj in rows[:count]:
    print(f'{size // 1024:7d} KB  {sec:9s} {sym:45s} {obj}')
