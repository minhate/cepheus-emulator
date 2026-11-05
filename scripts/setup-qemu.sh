#!/usr/bin/env bash
# Clone the pinned QEMU. Needs explicit network permission.
set -euo pipefail
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
[ -f "$ROOT/.env" ] && { set -a; . "$ROOT/.env"; set +a; }
. "$ROOT/config/pins.env"
: "${CEPHEUS_ALLOW_NETWORK:?set CEPHEUS_ALLOW_NETWORK=1 (in .env) to allow cloning QEMU}"
[ "$CEPHEUS_ALLOW_NETWORK" = 1 ] || { echo "network not allowed (CEPHEUS_ALLOW_NETWORK!=1)" >&2; exit 1; }
SRC="$ROOT/build/qemu-src"
if [ ! -d "$SRC/.git" ]; then
  mkdir -p "$ROOT/build"
  git clone --depth 1 --branch "$QEMU_REF" "$QEMU_REPO" "$SRC"
fi
echo "qemu-src $(git -C "$SRC" describe --tags --always) $(git -C "$SRC" rev-parse HEAD)"
