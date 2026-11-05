#!/bin/sh
# Builds and runs test_dxt.c natively (Linux, gcc), e.g. in the gcc:14 container.
set -e
cd "$(dirname "$0")/.."
gcc -O2 -std=gnu17 -Wall -I../../third_party/pbgl/include -o /tmp/test_dxt tests/test_dxt.c ../../third_party/pbgl/src/dxt.c -lm
/tmp/test_dxt
