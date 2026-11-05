#!/usr/bin/env bash
# Host readiness report. Prints what it actually finds; exits non-zero if required tools are missing.
set -u
ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if [ -f "$ROOT/.env" ]; then set -a; . "$ROOT/.env"; set +a; fi
missing=0
tool() { # tool <required|optional> <name> [version-arg]
  local kind=$1 name=$2 varg=${3:---version} path ver
  path=$(command -v "$name" 2>/dev/null) || { echo "$kind-missing $name"; [ "$kind" = required ] && missing=$((missing+1)); return; }
  ver=$("$path" $varg 2>&1 | head -n1)
  echo "tool $name $path $ver"
}
pkg() {
  local ver
  if command -v pkg-config >/dev/null 2>&1 && ver=$(pkg-config --modversion "$1" 2>/dev/null); then
    echo "pkg $1 $ver"
  else
    echo "required-missing pkg:$1"; missing=$((missing+1))
  fi
}
for t in git make gcc python3 ninja meson pkg-config dtc file cpio gzip xz; do tool required "$t"; done
tool required aarch64-linux-gnu-gcc
tool required qemu-img
pkg glib-2.0
pkg pixman-1
for t in simg2img lpunpack unpack_bootimg avbtool busybox; do tool optional "$t" --help; done
echo "DOCTOR missing=$missing"
[ "$missing" -eq 0 ]
