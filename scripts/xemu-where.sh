#!/usr/bin/env bash
# Samples where the CPU is in a running xemu (started with -s, GDB stub on
# port 1234) and resolves the addresses with the XBE's linker map.
#
# Usage: scripts/xemu-where.sh [samples] [map]
#   samples : how many times to attach (default 6)
#   map     : lld map file (default xbox/mohaa/build/main.map)
#
# For each sample it prints EIP and the return addresses found on the stack
# that point into the XBE's code.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
SAMPLES=${1:-6}
MAP=${2:-$ROOT/xbox/mohaa/build/main.map}
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/sample.gdb" <<'EOF'
set architecture i386
set pagination off
target remote host.docker.internal:1234
info registers eip
x/96wx $esp
detach
quit
EOF

TMPW="$(cd "$TMP" && (pwd -W 2>/dev/null || pwd))"
MSYS_NO_PATHCONV=1 docker run --rm -v "$TMPW:/s" ubuntu:24.04 sh -c "
    apt-get update -qq >/dev/null && apt-get install -y -qq gdb >/dev/null 2>&1
    for i in \$(seq $SAMPLES); do
        echo '=== sample'; gdb -q -batch -x /s/sample.gdb 2>&1 | grep -E '^eip|^0x'; sleep 0.5
    done" > "$TMP/samples.txt"

python -I - "$MAP" "$TMP/samples.txt" <<'PYEOF'
import bisect, re, sys

syms = []
for line in open(sys.argv[1], encoding='utf-8', errors='replace'):
    m = re.match(r'\s*0001:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8,16})\s+(?:f\s+)?(?:i\s+)?(\S+)', line)
    if m:
        syms.append((int(m.group(2), 16), m.group(1)))
syms.sort()
addrs = [a for a, _ in syms]
text_end = addrs[-1] + 0x10000 if addrs else 0

def name(a):
    if a >= 0x80000000:
        return 'kernel'
    i = bisect.bisect_right(addrs, a) - 1
    return f'{syms[i][1]}+{a - syms[i][0]:#x}' if i >= 0 else '?'

for block in open(sys.argv[2]).read().split('=== sample')[1:]:
    m = re.search(r'eip\s+0x([0-9a-f]+)', block)
    if not m:
        print('no connection')
        continue
    print('eip', name(int(m.group(1), 16)))
    for words in re.findall(r'0x[0-9a-f]+:\s+(.*)', block):
        for w in words.split():
            v = int(w, 16)
            if addrs and addrs[0] <= v < text_end:
                print('   <-', name(v))
PYEOF
