#!/usr/bin/env bash
# Build qemu-system-aarch64 (with our overlay) for real.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }

[ -d "$ROOT/build/qemu-src/.git" ] || { echo "run scripts/setup-qemu.sh first" >&2; exit 1; }
SRC="$(cd "$ROOT/build/qemu-src" && pwd -P)"
mkdir -p "$ROOT/build/qemu-build"
OUT="$(cd "$ROOT/build/qemu-build" && pwd -P)"

cd "$OUT"
[ -f build.ninja ] || "$SRC/configure" --target-list=aarch64-softmmu --disable-docs --extra-cflags="-O2"
ninja -j"${JOBS:-$(nproc)}"
echo "built: $OUT/qemu-system-aarch64"
