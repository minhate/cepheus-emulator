#!/usr/bin/env bash
# Attach gdb to a QEMU started with -s -S (port 1234).
# Usage: gdb-attach.sh [port] [gdb args, e.g. -batch -ex 'info registers pc']
set -euo pipefail
PORT="${1:-1234}"; shift || true
GDB="$(command -v gdb-multiarch || command -v gdb)"
exec "$GDB" -ex "set architecture aarch64" -ex "target remote :$PORT" "$@"
