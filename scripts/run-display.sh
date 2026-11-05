#!/usr/bin/env bash
# Open a host window (SDL + OpenGL). Machine type is always cepheus. Extra args (-kernel -dtb -drive ...) are yours.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
QEMU="${QEMU_BIN:-$ROOT/build/qemu-build-gfx/qemu-system-aarch64}"
[ -x "$QEMU" ] || QEMU="$ROOT/build/qemu-build/qemu-system-aarch64"
exec "$QEMU" -machine cepheus -display "${DISPLAY_BACKEND:-sdl,gl=on}" "$@"
