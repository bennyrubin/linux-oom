#!/bin/bash
# Fast incremental kernel build for QEMU development.
# Usage: ./build.sh [make args...]
# Example: ./build.sh         — full build
#          ./build.sh bzImage — just the kernel image
set -euo pipefail

LINUX_SRC="$(cd "$(dirname "$0")" && pwd)"
JOBS=$(nproc)

exec make \
    -C "$LINUX_SRC" \
    CC="ccache gcc" \
    LD="ld.lld" \
    -j"$JOBS" \
    "$@"
