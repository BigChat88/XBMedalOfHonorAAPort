#!/bin/sh
# Decodes $1 (an .ima file) with xbox_imastream.c and with ffmpeg and compares them.
# ffmpeg runs on the host: compare.py reads both outputs.
set -e
DIR=$(dirname "$0")
grep -v '^#include "../../../third_party' "$DIR/../../src/xbox_imastream.c" > "$DIR/imastream_body.inc"
clang -O1 -I"$DIR" -o "$2/imatest" "$DIR/imatest.c"
"$2/imatest" "$1" "$2/ours.pcm"
rm -f "$DIR/imastream_body.inc"
