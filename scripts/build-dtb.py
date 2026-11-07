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
    "qcom,spss_utils",
    "qcom,spcom",
    "qcom,ipa_fws",
    "qcom,kgsl-hyp",
    "usb_audio_qmi_dev",
    # camera_isp allowlisted stub (§3)
    "qcom,cam-req-mgr",
    "qcom,cam-res-mgr",
    "qcom,cam_smmu",
    "qcom,csiphy@ac65000",
    "qcom,csiphy@ac66000",
    "qcom,csiphy@ac67000",
    "qcom,csiphy@ac68000",
    "qcom,cci@ac4a000",
    "qcom,cci@ac4b000",
    "qcom,cam-cpas@ac40000",
    "qcom,cam-cdm-intf",
    "qcom,cpas-cdm0@ac48000",
    "qcom,cam-isp",
    "qcom,csid0@acb3000",
    "qcom,vfe0@acaf000",
    "qcom,csid1@acba000",
    "qcom,vfe1@acb6000",
    "qcom,csid-lite0@acc8000",
    "qcom,vfe-lite0@acc4000",
    "qcom,csid-lite1@accf000",
    "qcom,vfe-lite1@accb000",
    "qcom,cam-icp",
    "qcom,a5@ac00000",
    "qcom,ipe0",
    "qcom,ipe1",
    "qcom,bps",
    "qcom,cam-jpeg",
    "qcom,jpegenc@ac4e000",
    "qcom,cam-fd",
    "qcom,fd@ac5a000",
    "qcom,cam-lrme",
    "qcom,lrme@ac6b000",
]


def disable_node_in_dts(dts: str, node_name: str) -> str:
    pattern = re.compile(r"(^[ \t]*" + re.escape(node_name) + r"[ \t]*\{\n)", re.M)
    m = pattern.search(dts)
    if not m:
        return dts
    start_brace = m.end() - 2  # position of '{'
    depth = 0
    i = start_brace
    n = len(dts)
    while i < n:
        ch = dts[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                break
        i += 1
    if depth != 0:
        return dts
    block = dts[m.end():i]
    first_child = block.find("{")
    props_part = block[:first_child] if first_child >= 0 else block
    rest_part = block[first_child:] if first_child >= 0 else ""
    if re.search(r'^\s*status\s*=\s*"[^"]+"\s*;', props_part, re.M):
        props_part = re.sub(r'status\s*=\s*"[^"]+"\s*;', 'status = "disabled";', props_part)
    else:
        props_part = '\t\t\tstatus = "disabled";\n' + props_part
    rest_part = re.sub(r'status\s*=\s*"[^"]+"\s*;', 'status = "disabled";', rest_part)
    block = props_part + rest_part
    return dts[:m.end()] + block + dts[i:]


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
        dts = disable_node_in_dts(dts, node_name)

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
