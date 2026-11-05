#!/usr/bin/env python3
"""Stock firmware importer and lock validator.

Imports Xiaomi Mi 9 (cepheus) stock firmware into firmware/extracted,
computes real SHA-256 hashes of partition images, and compares them against
config/firmware.lock.json.
"""
import argparse, hashlib, json, os, shutil, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_INPUT = ROOT / "firmware" / "input"
DEFAULT_EXTRACTED = ROOT / "firmware" / "extracted"
DEFAULT_LOCK = ROOT / "config" / "firmware.lock.json"

KEY_IMAGES = [
    "boot.img",
    "dtbo.img",
    "vbmeta.img",
    "system.img",
    "vendor.img",
    "cust.img",
]


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_source_dir(input_dir: Path) -> Path:
    # 1. Direct files inside input_dir
    if (input_dir / "boot.img").is_file():
        return input_dir
    # 2. Inside a subdirectory or symlink
    for item in input_dir.iterdir():
        if item.is_dir() or item.is_symlink():
            if (item / "boot.img").is_file():
                return item
            images_sub = item / "images"
            if images_sub.is_dir() and (images_sub / "boot.img").is_file():
                return images_sub
    # 3. Check extracted dir if already populated
    if (DEFAULT_EXTRACTED / "boot.img").is_file():
        return DEFAULT_EXTRACTED
    raise FileNotFoundError(f"No stock firmware images found in {input_dir}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=DEFAULT_INPUT)
    parser.add_argument("--extracted", type=Path, default=DEFAULT_EXTRACTED)
    parser.add_argument("--lock", type=Path, default=DEFAULT_LOCK)
    parser.add_argument("--update-lock", action="store_true", help="Write computed hashes to lock file")
    args = parser.parse_args()

    args.extracted.mkdir(parents=True, exist_ok=True)

    # Check for archive
    archive_file = None
    for ext in ("*.tgz", "*.tar.gz", "*.tar", "*.zip"):
        archives = list(args.input.glob(ext))
        if archives:
            archive_file = archives[0]
            break

    if archive_file and archive_file.is_file():
        arch_hash = sha256_file(archive_file)
        print(f"archive sha256 {arch_hash} {archive_file.name}")
    else:
        # If no single archive file, indicate directory source
        print(f"archive sha256 0000000000000000000000000000000000000000000000000000000000000000 none")

    src_dir = find_source_dir(args.input)

    # Ensure files exist in extracted directory
    for img in KEY_IMAGES:
        target = args.extracted / img
        src = src_dir / img
        if src.is_file() and not target.exists():
            try:
                os.link(src, target)
            except OSError:
                try:
                    os.symlink(src, target)
                except OSError:
                    shutil.copy2(src, target)

    # Compute hashes of key images
    computed = {}
    for img in KEY_IMAGES:
        target = args.extracted / img
        if target.is_file():
            h = sha256_file(target)
            computed[img] = h
            print(f"{img} sha256 {h} {target}")
        else:
            print(f"{img} missing in {args.extracted}")

    # Load lock file
    lock_data = {}
    if args.lock.is_file():
        try:
            lock_data = json.loads(args.lock.read_text())
        except Exception as e:
            print(f"Error reading lock file: {e}", file=sys.stderr)

    locked_images = lock_data.get("images", {})

    if args.update_lock:
        lock_data.setdefault("schema", 1)
        lock_data.setdefault("device", "cepheus")
        lock_data.setdefault("build_id", "V12.5.6.0.RFACNXM")
        lock_data.setdefault("android_version", "11")
        if archive_file:
            lock_data["archive_sha256"] = arch_hash
        lock_data["images"] = computed
        args.lock.write_text(json.dumps(lock_data, indent=2) + "\n")
        print(f"Updated {args.lock}")
        locked_images = computed

    # Compare against lock
    all_equal = True
    for img in ("boot.img", "vbmeta.img", "dtbo.img", "system.img", "vendor.img"):
        if img in computed and img in locked_images:
            if computed[img] == locked_images[img]:
                print(f"lock-compare {img} equal")
            else:
                print(f"lock-compare {img} unequal")
                all_equal = False
        elif img in computed and not locked_images:
            # If lock is empty and not updating lock
            print(f"lock-compare {img} unconfigured")
            all_equal = False
        else:
            print(f"lock-compare {img} missing")
            all_equal = False

    return 0 if all_equal else 1


if __name__ == "__main__":
    sys.exit(main())
