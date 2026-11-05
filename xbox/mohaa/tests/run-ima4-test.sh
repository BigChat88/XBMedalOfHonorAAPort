#!/bin/sh
# Builds and runs test_ima4.c natively (Linux, gcc), e.g. in the gcc:14 container.
set -e
cd "$(dirname "$0")/.."
gcc -O2 -std=gnu17 -Wall -o /tmp/test_ima4 tests/test_ima4.c src/xbox_ima4.c -lm
/tmp/test_ima4
