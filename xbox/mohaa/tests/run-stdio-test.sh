#!/bin/sh
# Builds and runs test_stdio.c natively (Linux, gcc) against glibc.
set -e
cd "$(dirname "$0")/.."
P=../../third_party/printf/src
gcc -O2 -std=gnu17 -w -DPRINTF_SUPPORT_LONG_LONG=1 -DPRINTF_MAX_INTEGRAL_DIGITS_FOR_DECIMAL=18 -DPRINTF_DECIMAL_BUFFER_SIZE=64 -DPRINTF_LOG10_TAYLOR_TERMS=8 -o /tmp/test_stdio tests/test_stdio.c src/xbox_stdio.c $P/printf/printf.c -I$P -lm
/tmp/test_stdio
