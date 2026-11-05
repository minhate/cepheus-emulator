#!/usr/bin/env bash
# Second QEMU build dir with OpenGL + SDL so the HOST GPU presents/scales the guest window.
# Same source + overlay as build-qemu.sh. This does NOT accelerate guest rendering (see docs/ADDON.md).
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }

[ -d "$ROOT/build/qemu-src/.git" ] || { echo "run scripts/setup-qemu.sh and scripts/apply-overlay.sh first" >&2; exit 1; }
SRC="$(cd "$ROOT/build/qemu-src" && pwd -P)"
mkdir -p "$ROOT/build/qemu-build-gfx"
OUT="$(cd "$ROOT/build/qemu-build-gfx" && pwd -P)"

cd "$OUT"
[ -f build.ninja ] || "$SRC/configure" --target-list=aarch64-softmmu --disable-docs --enable-debug --enable-opengl --enable-sdl
ninja -j"${JOBS:-$(nproc)}"
echo "built: $OUT/qemu-system-aarch64"
