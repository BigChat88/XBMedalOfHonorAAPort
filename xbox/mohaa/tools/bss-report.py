"""Lists the largest static variables (.data/.bss) of the XBE from the lld map.

The map is MSVC-style (no symbol sizes), so each size is the distance to the
next symbol in section 0003.

Usage: python tools/bss-report.py build/main.map [count]
"""
import re
import sys

path = sys.argv[1]
count = int(sys.argv[2]) if len(sys.argv) > 2 else 30
lines = open(path, encoding='utf-8', errors='replace').read().splitlines()

end = 0
for line in lines:
    m = re.match(r'\s*0003:([0-9a-f]{8})\s+([0-9a-f]{8})H\s+\.\S+', line)
    if m:
        end = max(end, int(m.group(1), 16) + int(m.group(2), 16))

syms = []
for line in lines:
    m = re.match(r'\s*0003:([0-9a-f]{8})\s+(\S+)\s+[0-9a-f]+\s+(?:f\s+)?(?:i\s+)?(\S+)?', line)
    if m:
        syms.append((int(m.group(1), 16), m.group(2), m.group(3) or ''))
syms.sort()

sizes = []
for i, (addr, name, obj) in enumerate(syms):
    nxt = syms[i + 1][0] if i + 1 < len(syms) else end
    sizes.append((nxt - addr, name, obj))
sizes.sort(reverse=True)

print(f'.data + .bss = {end / 2**20:.2f} MB')
for size, name, obj in sizes[:count]:
    print(f'{size / 2**20:6.2f} MB  {name[:64]:64} {obj}')
