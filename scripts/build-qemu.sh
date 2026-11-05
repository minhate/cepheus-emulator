#!/usr/bin/env bash
# Build qemu-system-aarch64 (with our overlay) for real.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
SRC="$ROOT/build/qemu-src"; OUT="$ROOT/build/qemu-build"
[ -d "$SRC/.git" ] || { echo "run scripts/setup-qemu.sh first" >&2; exit 1; }
mkdir -p "$OUT"; cd "$OUT"
[ -f build.ninja ] || "$SRC/configure" --target-list=aarch64-softmmu --disable-docs --enable-debug
ninja -j"${JOBS:-$(nproc)}"
echo "built: $OUT/qemu-system-aarch64"
