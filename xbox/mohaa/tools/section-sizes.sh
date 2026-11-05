#!/bin/sh
# Prints the total .data/.bss size of a set of objects (in KB).
# Usage: section-sizes.sh <obj...>
llvm-objdump -h "$@" 2>/dev/null | awk '
    function hex(s,   i, n) {
        n = 0
        for (i = 1; i <= length(s); i++)
            n = n * 16 + index("0123456789abcdef", tolower(substr(s, i, 1))) - 1
        return n
    }
    $2 == ".data" || $2 == ".bss" { sz[$2] += hex($3) }
    END { for (k in sz) printf "%s %d KB\n", k, sz[k] / 1024 }'
