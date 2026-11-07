#!/usr/bin/env python3
"""Build real UFS LUN images (lun0.raw .. lun5.raw) from stock firmware rawprogram/patch XMLs.

Uses real partition tables (gpt_main0..5.bin, gpt_backup0..5.bin), applies Qualcomm
Firehose patch0..5.xml (updating LastUseableLBA, BackupGPT LBA, last partition size,
and IEEE CRC32 checksums), writes real partition images at their exact 4096-byte sector
offsets, and prepares the stock ramdisk + command-only probe for R8.
"""
import argparse
import gzip
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_FW = ROOT / "firmware" / "extracted"
DEFAULT_OUT = ROOT / "build" / "luns"
DEFAULT_STOCK = ROOT / "build" / "stock"
SECTOR_SIZE = 4096

# Total 4KB sectors per UFS LUN matching stock layout + room for growable last partition
LUN_DISK_SECTORS = {
    0: 3653637,  # 507904 + 3145728 (12GB userdata) + 5 backup GPT sectors
    1: 2048,     # 8 MB (xbl_config, xbl, last_parti)
    2: 2048,     # 8 MB (xbl_configbak, xblbak, last_parti)
    3: 2048,     # 8 MB (ddr/cdt/bk partitions, last_parti)
    4: 1572864,  # 6 GB (all boot/tz/hyp/abl/dtbo/vbmeta/vendor/system partitions)
    5: 8192,     # 32 MB (fsc/fsg/modemst1/modemst2/imagefv, last_parti)
}

SPARSE_HEADER_MAGIC = 0xED26FF3A
CHUNK_TYPE_RAW = 0xCAC1
CHUNK_TYPE_FILL = 0xCAC2
CHUNK_TYPE_DONT_CARE = 0xCAC3
CHUNK_TYPE_CRC32 = 0xCAC4


def eval_sector_expr(expr: str, num_disk_sectors: int) -> int:
    s = expr.strip().rstrip(".")
    if s.startswith("NUM_DISK_SECTORS"):
        rest = s[len("NUM_DISK_SECTORS"):].strip()
        if not rest:
            return num_disk_sectors
        return num_disk_sectors + int(rest)
    return int(s)


def apply_firehose_patches(fd: int, patch_xml: Path, num_disk_sectors: int) -> None:
    tree = ET.parse(patch_xml)
    for p in tree.getroot().findall("patch"):
        if p.attrib.get("filename") != "DISK":
            continue
        start_sec = eval_sector_expr(p.attrib["start_sector"], num_disk_sectors)
        byte_off = int(p.attrib["byte_offset"])
        size_bytes = int(p.attrib["size_in_bytes"])
        val_str = p.attrib["value"].strip()

        target_pos = start_sec * SECTOR_SIZE + byte_off
        if val_str.startswith("CRC32("):
            m = re.match(r"CRC32\(([^,]+),([0-9]+)\)", val_str)
            if not m:
                raise ValueError(f"Invalid CRC32 expression: {val_str}")
            crc_sec = eval_sector_expr(m.group(1), num_disk_sectors)
            crc_len = int(m.group(2))
            data = os.pread(fd, crc_len, crc_sec * SECTOR_SIZE)
            val = zlib.crc32(data) & 0xFFFFFFFF
        else:
            val = eval_sector_expr(val_str, num_disk_sectors)

        if size_bytes == 4:
            buf = struct.pack("<I", val & 0xFFFFFFFF)
        elif size_bytes == 8:
            buf = struct.pack("<Q", val & 0xFFFFFFFFFFFFFFFF)
        else:
            raise ValueError(f"Unsupported patch size_in_bytes={size_bytes}")
        os.pwrite(fd, buf, target_pos)


def write_sparse_file_to_lun(fd: int, src_path: Path, dest_offset: int, max_chunks: int = 0) -> None:
    """Write an Android sparse image into fd at dest_offset, using simg2img for small images
    or direct chunk unpacking to preserve sparse holes on disk."""
    file_sz = src_path.stat().st_size
    if file_sz < 4 * 1024 * 1024 and max_chunks == 0:
        with tempfile.NamedTemporaryFile(dir=src_path.parent if os.access(src_path.parent, os.W_OK) else None, delete=False) as tf:
            tmp_raw = Path(tf.name)
        try:
            subprocess.run(["simg2img", str(src_path), str(tmp_raw)], check=True)
            with open(tmp_raw, "rb") as rf:
                pos = 0
                while True:
                    chunk = rf.read(1024 * 1024)
                    if not chunk:
                        break
                    if any(chunk):
                        os.pwrite(fd, chunk, dest_offset + pos)
                    pos += len(chunk)
        finally:
            tmp_raw.unlink(missing_ok=True)
        return

    with open(src_path, "rb") as sf:
        hdr = sf.read(28)
        magic, maj, min_v, fhdr_sz, chdr_sz, blk_sz, total_blks, total_chunks, _ = struct.unpack("<IHHHHIIII", hdr)
        if magic != SPARSE_HEADER_MAGIC:
            raise ValueError(f"Invalid sparse magic in {src_path}: 0x{magic:08x}")
        if fhdr_sz > 28:
            sf.seek(fhdr_sz - 28, os.SEEK_CUR)

        out_pos = dest_offset
        limit = total_chunks if max_chunks <= 0 else min(total_chunks, max_chunks)
        for _ in range(limit):
            chdr = sf.read(12)
            if len(chdr) < 12:
                break
            ctype, _, cblks, csz = struct.unpack("<HHII", chdr)
            if chdr_sz > 12:
                sf.seek(chdr_sz - 12, os.SEEK_CUR)
            data_sz = cblks * blk_sz
            if ctype == CHUNK_TYPE_RAW:
                rem = data_sz
                while rem > 0:
                    step = min(rem, 1024 * 1024)
                    buf = sf.read(step)
                    if not buf:
                        break
                    if any(buf):
                        os.pwrite(fd, buf, out_pos)
                    out_pos += len(buf)
                    rem -= len(buf)
            elif ctype == CHUNK_TYPE_FILL:
                fill_bytes = sf.read(4)
                if fill_bytes != b"\x00\x00\x00\x00":
                    block = fill_bytes * (min(data_sz, 65536) // 4)
                    rem = data_sz
                    while rem > 0:
                        step = min(rem, len(block))
                        os.pwrite(fd, block[:step], out_pos)
                        out_pos += step
                        rem -= step
                else:
                    out_pos += data_sz
            elif ctype == CHUNK_TYPE_DONT_CARE:
                out_pos += data_sz
            elif ctype == CHUNK_TYPE_CRC32:
                sf.read(4)
            else:
                sf.seek(csz - chdr_sz, os.SEEK_CUR)


def verify_gpt(fd: int, num_disk_sectors: int) -> int:
    hdr = os.pread(fd, SECTOR_SIZE, SECTOR_SIZE)
    sig, rev, hdr_sz, crc, rsv, cur_lba, bkp_lba, first_u, last_u, guid, part_lba, num_parts, part_sz, part_crc = struct.unpack_from(
        "<8sIIIIQQQQ16sQIII", hdr, 0
    )
    if sig != b"EFI PART":
        raise RuntimeError(f"Primary GPT signature mismatch: {sig!r}")
    hdr_zero_crc = bytearray(hdr[:hdr_sz])
    struct.pack_into("<I", hdr_zero_crc, 16, 0)
    calc_hdr_crc = zlib.crc32(hdr_zero_crc) & 0xFFFFFFFF
    if calc_hdr_crc != crc:
        raise RuntimeError(f"Primary GPT header CRC mismatch: expected 0x{crc:08x}, got 0x{calc_hdr_crc:08x}")

    part_arr = os.pread(fd, num_parts * part_sz, part_lba * SECTOR_SIZE)
    calc_part_crc = zlib.crc32(part_arr) & 0xFFFFFFFF
    if calc_part_crc != part_crc:
        raise RuntimeError(f"Primary GPT partition array CRC mismatch: expected 0x{part_crc:08x}, got 0x{calc_part_crc:08x}")

    active = 0
    for i in range(num_parts):
        entry = part_arr[i * part_sz : (i + 1) * part_sz]
        if any(entry[:16]):
            active += 1
    return active


def build_lun(lun_idx: int, fw_dir: Path, out_dir: Path, full_sparse: bool = False) -> Path:
    num_sectors = LUN_DISK_SECTORS[lun_idx]
    lun_path = out_dir / f"lun{lun_idx}.raw"
    rawprog_path = fw_dir / f"rawprogram{lun_idx}.xml"
    patch_path = fw_dir / f"patch{lun_idx}.xml"

    fd = os.open(lun_path, os.O_CREAT | os.O_TRUNC | os.O_RDWR, 0o644)
    try:
        os.ftruncate(fd, num_sectors * SECTOR_SIZE)
        tree = ET.parse(rawprog_path)
        for prog in tree.getroot().findall("program"):
            fn = prog.attrib.get("filename", "")
            if not fn:
                continue
            src = fw_dir / fn
            if not src.is_file():
                continue
            start_sec = eval_sector_expr(prog.attrib["start_sector"], num_sectors)
            dest_off = start_sec * SECTOR_SIZE
            is_sparse = prog.attrib.get("sparse", "false").lower() == "true"

            if is_sparse:
                # Unsparse metadata & cache completely via simg2img; for multi-GB images,
                # unpack initial chunks (superblock + root metadata) unless --full is passed.
                max_chunks = 0 if (full_sparse or fn in ("metadata.img", "cache.img")) else 64
                write_sparse_file_to_lun(fd, src, dest_off, max_chunks=max_chunks)
            else:
                with open(src, "rb") as rf:
                    pos = 0
                    while True:
                        buf = rf.read(1024 * 1024)
                        if not buf:
                            break
                        if any(buf):
                            os.pwrite(fd, buf, dest_off + pos)
                        pos += len(buf)

        apply_firehose_patches(fd, patch_path, num_sectors)
        active_parts = verify_gpt(fd, num_sectors)
        print(f"lun{lun_idx}.raw: {num_sectors} sectors ({num_sectors * 4 // 1024} MB), {active_parts} GPT partitions verified")
    finally:
        os.close(fd)
    return lun_path


def make_cpio_newc_entry(name: str, mode: int, data: bytes = b"", rdev_major: int = 0, rdev_minor: int = 0, ino: int = 1) -> bytes:
    name_b = name.encode("utf-8") + b"\x00"
    namesize = len(name_b)
    filesize = len(data)
    hdr = (
        f"070701"
        f"{ino:08X}"
        f"{mode:08X}"
        f"{0:08X}"
        f"{0:08X}"
        f"{1:08X}"
        f"{0:08X}"
        f"{filesize:08X}"
        f"{0:08X}"
        f"{0:08X}"
        f"{rdev_major:08X}"
        f"{rdev_minor:08X}"
        f"{namesize:08X}"
        f"{0:08X}"
    ).encode("ascii")
    out = bytearray(hdr) + name_b
    while len(out) % 4 != 0:
        out.append(0)
    out += data
    while len(out) % 4 != 0:
        out.append(0)
    return bytes(out)


def build_stock_ramdisk_and_probe(fw_dir: Path, stock_dir: Path, out_dir: Path) -> None:
    rec_path = fw_dir / "recovery.img"
    if not rec_path.is_file():
        raise FileNotFoundError(f"Missing stock recovery.img at {rec_path}")

    with open(rec_path, "rb") as f:
        hdr = f.read(4096)
        magic, k_sz, k_addr, r_sz, r_addr, s_sz, s_addr, t_addr, pg_sz, v, os_v = struct.unpack("<8s10I", hdr[:48])
        if magic != b"ANDROID!" or r_sz == 0:
            raise ValueError("Invalid recovery.img header or empty ramdisk")
        k_pages = (k_sz + pg_sz - 1) // pg_sz
        f.seek((1 + k_pages) * pg_sz)
        ramdisk_gz = f.read(r_sz)

    stock_dir.mkdir(parents=True, exist_ok=True)
    stock_ramdisk = stock_dir / "ramdisk.cpio.gz"
    stock_ramdisk.write_bytes(ramdisk_gz)

    probe_src = ROOT / "guest" / "probes" / "r8_partitions.sh"
    probe_bytes = probe_src.read_bytes()

    extra_cpio = bytearray()
    extra_cpio += make_cpio_newc_entry("dev", 0o040755, ino=900001)
    extra_cpio += make_cpio_newc_entry("dev/console", 0o020600, rdev_major=1, rdev_minor=11, ino=900002)
    extra_cpio += make_cpio_newc_entry("dev/kmsg", 0o020600, rdev_major=1, rdev_minor=11, ino=900003)
    extra_cpio += make_cpio_newc_entry("dev/null", 0o020666, rdev_major=1, rdev_minor=3, ino=900004)
    extra_cpio += make_cpio_newc_entry("dev/zero", 0o020666, rdev_major=1, rdev_minor=5, ino=900005)
    extra_cpio += make_cpio_newc_entry("dev/random", 0o020666, rdev_major=1, rdev_minor=8, ino=900006)
    extra_cpio += make_cpio_newc_entry("dev/urandom", 0o020666, rdev_major=1, rdev_minor=9, ino=900007)
    extra_cpio += make_cpio_newc_entry("probe.sh", 0o100755, data=probe_bytes, ino=900008)
    extra_cpio += make_cpio_newc_entry("TRAILER!!!", 0, ino=0)

    probe_initrd = out_dir / "probe_r8.cpio.gz"
    raw_cpio = gzip.decompress(ramdisk_gz)
    trailer_pos = raw_cpio.rfind(b"070701")
    if trailer_pos > 0 and b"TRAILER!!!" in raw_cpio[trailer_pos:trailer_pos + 140]:
        combined_cpio = raw_cpio[:trailer_pos] + bytes(extra_cpio)
    else:
        combined_cpio = raw_cpio + bytes(extra_cpio)
    probe_initrd.write_bytes(gzip.compress(combined_cpio, compresslevel=1))
    print(f"Built {probe_initrd.relative_to(ROOT)} (stock ramdisk + guest/probes/r8_partitions.sh)")


def build_corrupt_luns(corrupt_dir: Path) -> None:
    corrupt_dir.mkdir(parents=True, exist_ok=True)
    for i in range(6):
        num_sectors = LUN_DISK_SECTORS[i]
        p = corrupt_dir / f"lun{i}.raw"
        fd = os.open(p, os.O_CREAT | os.O_TRUNC | os.O_RDWR, 0o644)
        try:
            os.ftruncate(fd, num_sectors * SECTOR_SIZE)
            # Write corrupted GPT signature at primary and backup GPT locations
            os.pwrite(fd, b"CORRUPT!" * 64, SECTOR_SIZE)
            os.pwrite(fd, b"CORRUPT!" * 64, (num_sectors - 1) * SECTOR_SIZE)
        finally:
            os.close(fd)
    print(f"Built corrupted GPT LUNs in {corrupt_dir.relative_to(ROOT)}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--fw-dir", type=Path, default=DEFAULT_FW)
    ap.add_argument("--out-dir", type=Path, default=DEFAULT_OUT)
    ap.add_argument("--stock-dir", type=Path, default=DEFAULT_STOCK)
    ap.add_argument("--corrupt-dir", type=Path, default=ROOT / "build" / "luns-corrupt")
    ap.add_argument("--full", action="store_true", help="Unsparse full multi-GB system/vendor/userdata images")
    ap.add_argument("--probe-only", action="store_true", help="Only rebuild stock ramdisk and probe_r8.cpio.gz")
    args = ap.parse_args()

    args.out_dir.mkdir(parents=True, exist_ok=True)
    if not args.probe_only:
        for i in range(6):
            build_lun(i, args.fw_dir, args.out_dir, full_sparse=args.full)
    build_stock_ramdisk_and_probe(args.fw_dir, args.stock_dir, args.out_dir)
    if args.corrupt_dir and not args.probe_only:
        build_corrupt_luns(args.corrupt_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
