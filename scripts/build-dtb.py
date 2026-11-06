#!/usr/bin/env python3
"""Build the patched Xiaomi Mi 9 (cepheus) DTB from stock DTB and stock DTBO.

Extracts DTBO #5 (matching CEPHEUS) from firmware/extracted/dtbo.img, overlays it
onto build/stock/dtb.bin, and disables allowlisted stubs (§3: modem_dsp, camera_isp)
to prevent unmodeled peripheral loader / error unwind kernel panics.
Outputs to build/dtb/cepheus.dtb.
"""
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(os.environ.get("CEPHEUS_ROOT") or Path(__file__).resolve().parents[1])
STOCK_DTB = ROOT / "build" / "stock" / "dtb.bin"
STOCK_DTBO = ROOT / "firmware" / "extracted" / "dtbo.img"
OUT_DIR = ROOT / "build" / "dtb"
OUT_DTB = OUT_DIR / "cepheus.dtb"

NODES_TO_DISABLE = [
    # modem_dsp allowlisted stub (§3)
    "qcom,mss@4080000",
    "qcom,lpass@17300000",
    "qcom,ssc@5c00000",
    "qcom,turing@8300000",
    "qcom,venus@aae0000",
    "qcom,npu@0x9800000",
    "qcom,spss@1880000",
    "qcom,ipa_fws",
    "qcom,kgsl-hyp",
    # camera_isp allowlisted stub (§3)
    "qcom,cam-req-mgr",
    "qcom,csiphy@ac65000",
    "qcom,cam-cpas@ac40000",
    "qcom,cam-cdm-intf",
    "qcom,cam-isp",
    "qcom,cam-icp",
    "qcom,cam-jpeg",
    "qcom,cam-fd",
    "qcom,cam-lrme",
]


def extract_dtbo5(dtbo_path: Path) -> bytes:
    with open(dtbo_path, "rb") as f:
        hdr = f.read(32)
        _, _, _, dt_entry_size, dt_entry_count, dt_entries_offset, _, _ = struct.unpack(">8I", hdr)
        f.seek(dt_entries_offset + 5 * dt_entry_size)
        dt_size, dt_offset = struct.unpack(">2I", f.read(8))
        f.seek(dt_offset)
        return f.read(dt_size)


def main() -> int:
    if not STOCK_DTB.is_file():
        print(f"Error: {STOCK_DTB} not found. Run extract-boot.py first.", file=sys.stderr)
        return 1
    if not STOCK_DTBO.is_file():
        print(f"Error: {STOCK_DTBO} not found.", file=sys.stderr)
        return 1

    OUT_DIR.mkdir(parents=True, exist_ok=True)
    tmp_dtbo = OUT_DIR / "dtbo5.dtb"
    tmp_overlay = OUT_DIR / "cepheus_merged.dtb"
    tmp_dts = OUT_DIR / "cepheus_merged.dts"

    # 1. Extract DTBO #5
    tmp_dtbo.write_bytes(extract_dtbo5(STOCK_DTBO))

    # 2. Overlay DTBO #5 onto stock base DTB
    subprocess.run(["fdtoverlay", "-i", str(STOCK_DTB), "-o", str(tmp_overlay), str(tmp_dtbo)], check=True)

    # 3. Decompile to DTS to disable allowlisted nodes
    res = subprocess.run(["dtc", "-I", "dtb", "-O", "dts", str(tmp_overlay)], capture_output=True, check=True)
    dts = res.stdout.decode("utf-8", "replace")

    for node_name in NODES_TO_DISABLE:
        pos = dts.find(node_name + " {")
        if pos >= 0:
            end = dts.find("};", pos)
            block = dts[pos:end]
            if "status =" in block:
                block = re.sub(r'status\s*=\s*"[^"]+"', 'status = "disabled"', block)
            else:
                block = block + '\n\t\t\tstatus = "disabled";\n'
            dts = dts[:pos] + block + dts[end:]

    tmp_dts.write_text(dts)

    # 4. Compile final DTB
    subprocess.run(["dtc", "-I", "dts", "-O", "dtb", "-o", str(OUT_DTB), str(tmp_dts)], check=True)
    print(f"Built native cepheus DTB: {OUT_DTB} ({OUT_DTB.stat().st_size} bytes)")

    # Clean intermediate files
    for p in (tmp_dtbo, tmp_overlay, tmp_dts):
        if p.exists():
            p.unlink()

    return 0


if __name__ == "__main__":
    sys.exit(main())
