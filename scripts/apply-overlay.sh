#!/usr/bin/env bash
# Apply our overlay + patches onto the pinned QEMU checkout (idempotent).
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
SRC="$ROOT/build/qemu-src"
[ -d "$SRC/.git" ] || { echo "run scripts/setup-qemu.sh first" >&2; exit 1; }
cp -a "$ROOT/qemu/overlay/." "$SRC/"
shopt -s nullglob
for p in "$ROOT"/qemu/patches/*.patch; do
  if git -C "$SRC" apply -R --check "$p" >/dev/null 2>&1; then echo "already applied: $(basename "$p")"; continue; fi
  git -C "$SRC" apply --check "$p"
  git -C "$SRC" apply "$p"
  echo "applied: $(basename "$p")"
done
