#!/usr/bin/env bash
# Host display/debug readiness. Prints what is really there. Separate from doctor.sh (R0 pattern unchanged).
set -u
missing=0
pk() { # pk <required|optional> <pkg>
  local v
  if command -v pkg-config >/dev/null 2>&1 && v=$(pkg-config --modversion "$2" 2>/dev/null); then echo "pkg $2 $v"
  else echo "$1-missing pkg:$2"; [ "$1" = required ] && missing=$((missing+1)); fi
}
pk required sdl2; pk required epoxy; pk optional gbm
if command -v nvidia-smi >/dev/null 2>&1; then echo "gpu $(nvidia-smi -L 2>&1 | head -n1)"; else echo "optional-missing nvidia-smi"; fi
if command -v glxinfo >/dev/null 2>&1; then glxinfo -B 2>/dev/null | grep -m1 'OpenGL renderer' || echo "optional-missing glx-renderer"; else echo "optional-missing glxinfo"; fi
if command -v gdb-multiarch >/dev/null 2>&1; then echo "tool gdb-multiarch $(command -v gdb-multiarch)"
elif command -v gdb >/dev/null 2>&1; then echo "tool gdb $(command -v gdb)"
else echo "required-missing gdb"; missing=$((missing+1)); fi
for t in adb fastboot; do
  if command -v "$t" >/dev/null 2>&1; then echo "tool $t $(command -v "$t")"; else echo "required-missing $t"; missing=$((missing+1)); fi
done
echo "DOCTOR-GFX missing=$missing"
[ "$missing" -eq 0 ]
