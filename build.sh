#!/bin/sh
# build.sh -- build of the Xbox game (Linux/macOS). See build.cmd / README.md.
#   ./build.sh                 # takes the game from gamedata/
#   ./build.sh --no-music      # options go straight to tools/pack_iso.py
set -e
cd "$(dirname "$0")"
PY="${PYTHON:-python3}"
"$PY" -m pip install --quiet --disable-pip-version-check -r requirements.txt || \
    echo "warning: could not install imageio-ffmpeg/Pillow (pip); the game will play MP3s and have no cover"
exec "$PY" tools/pack_iso.py "$@"
