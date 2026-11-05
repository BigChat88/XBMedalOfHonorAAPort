#!/usr/bin/env bash
# Runs a shell command inside the nxdk container with the repo mounted at /work.
# Usage: scripts/nxdk.sh xbox/mohaa 'make -j12 objs'
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && (pwd -W 2>/dev/null || pwd))"
DIR="$1"; shift
MSYS_NO_PATHCONV=1 exec docker run --rm -v "$ROOT:/work" -w "/work/$DIR" -e NXDK_DIR=/usr/src/nxdk \
    ghcr.io/xboxdev/nxdk:latest sh -c "$*"
