#!/usr/bin/env python3
"""Extract stock kernel Image and Device Tree Blobs (DTB) from Android boot.img.

Extracts components into build/stock/, decompiles DTB to DTS using dtc,
and prints the kernel version banner and DTB compatible property.
"""
import argparse, os, re, struct, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BOOT = ROOT / "firmware" / "extracted" / "boot.img"
DEFAULT_OUT = ROOT / "build" / "stock"
ARM64_MAGIC = 0x644D5241  # 'ARM\x64' in little-endian
DTB_MAGIC = b"\xd0\x0d\xfe\xed"


def extract_kernel_and_dtb(boot_path: Path, out_dir: Path) -> int:
    if not boot_path.is_file():
        print(f"Error: boot image not found: {boot_path}", file=sys.stderr)
        return 1

    file_size = boot_path.stat().st_size
    if file_size < 4096:
        print(f"Error: boot image too small ({file_size} bytes)", file=sys.stderr)
        return 1

    with open(boot_path, "rb") as f:
        header = f.read(4096)

    if header[:8] != b"ANDROID!":
        print(f"Error: invalid boot.img magic: {header[:8]!r}", file=sys.stderr)
        return 1

    magic, k_sz, k_addr, r_sz, r_addr, s_sz, s_addr, t_addr, pg_sz, v, os_v = struct.unpack("<8s10I", header[:48])
    cmdline = header[64:576].rstrip(b"\0").decode(errors="replace")

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "cmdline.txt").write_text(cmdline + "\n")

    # Read kernel and following pages
    with open(boot_path, "rb") as f:
        f.seek(pg_sz)
        kernel_area = f.read(k_sz)

    # Locate ARM64 kernel Image
    arm64_pos = kernel_area.find(b"ARM\x64")
    if arm64_pos < 0x38:
        print("Error: ARM64 kernel header magic not found in kernel payload", file=sys.stderr)
        return 1

    image_start = arm64_pos - 0x38
    # Search for DTB magic after image_start
    dtb_pos = kernel_area.find(DTB_MAGIC, image_start)

    if dtb_pos > image_start:
        kernel_bytes = kernel_area[image_start:dtb_pos]
        dtb_area = kernel_area[dtb_pos:]
    else:
        kernel_bytes = kernel_area[image_start:]
        dtb_area = b""

    # Save kernel Image
    image_path = out_dir / "Image"
    image_path.write_bytes(kernel_bytes)

    # Search kernel banner
    ver_match = re.search(rb"Linux version 4\.14\.[0-9]+[^\x00\r\n]+", kernel_bytes)
    if ver_match:
        ver_str = ver_match.group(0).decode("utf-8", errors="replace")
        print(ver_str)
    else:
        print("Error: Linux kernel version string not found in Image", file=sys.stderr)
        return 1

    # Extract DTB
    if not dtb_area:
        # Check if DTB is appended in boot.img after kernel
        with open(boot_path, "rb") as f:
            f.seek(pg_sz + k_sz)
            extra = f.read(4 * 1024 * 1024)
            dtb_pos_extra = extra.find(DTB_MAGIC)
            if dtb_pos_extra >= 0:
                dtb_area = extra[dtb_pos_extra:]

    if not dtb_area:
        print("Error: Device tree blob (DTB) not found in boot.img", file=sys.stderr)
        return 1

    dtb_size = int.from_bytes(dtb_area[4:8], "big")
    dtb_bytes = dtb_area[:dtb_size]

    # If first DTB is variant sm8150p, also find sm8150 if present
    dtb_path = out_dir / "dtb.bin"
    dtb_path.write_bytes(dtb_bytes)

    # Decompile with dtc
    try:
        proc = subprocess.run(["dtc", "-I", "dtb", "-O", "dts", "-o", str(out_dir / "dtb.dts"), str(dtb_path)],
                              capture_output=True, text=True, check=True)
    except subprocess.CalledProcessError as e:
        print(f"Error decompiling DTB: {e.stderr}", file=sys.stderr)
        return 1

    # Print compatible property from root node of decompiled DTS
    dts_text = (out_dir / "dtb.dts").read_text(errors="replace")
    comp_match = re.search(r'compatible\s*=\s*("[^"]*qcom,sm8150[^"]*")', dts_text)
    if comp_match:
        print(f'compatible = {comp_match.group(1)};')
    else:
        # Check all DTBs in dtb_area for exact qcom,sm8150
        found = False
        pos = 0
        while True:
            p = dtb_area.find(DTB_MAGIC, pos)
            if p < 0:
                break
            sz = int.from_bytes(dtb_area[p+4:p+8], "big")
            cur_dtb = dtb_area[p:p+sz]
            pos = p + sz
            try:
                res = subprocess.run(["dtc", "-I", "dtb", "-O", "dts"], input=cur_dtb, capture_output=True, check=True)
                m = re.search(r'compatible\s*=\s*("[^"]*qcom,sm8150[^"]*")', res.stdout.decode(errors="replace"))
                if m:
                    print(f'compatible = {m.group(1)};')
                    found = True
                    break
            except Exception:
                pass
        if not found:
            print("Error: sm8150 compatible property not found in DTB", file=sys.stderr)
            return 1

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, default=DEFAULT_BOOT)
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT)
    args = parser.parse_args()

    return extract_kernel_and_dtb(args.boot, args.out)


if __name__ == "__main__":
    sys.exit(main())
