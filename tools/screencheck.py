#!/usr/bin/env python3
"""Analyze QEMU screendumps (PPM P6). Facts are computed from real pixels; optional PNG export for humans.
Usage: screencheck.py A.ppm [B.ppm] [--png-dir DIR]"""
import argparse, hashlib, struct, sys, zlib
from collections import Counter
from pathlib import Path


def read_ppm(path):
    data = Path(path).read_bytes()
    pos = 0

    def tok():
        nonlocal pos
        while pos < len(data):
            c = data[pos:pos + 1]
            if c == b"#":
                while pos < len(data) and data[pos:pos + 1] != b"\n":
                    pos += 1
            elif c.isspace():
                pos += 1
            else:
                break
        s = pos
        while pos < len(data) and not data[pos:pos + 1].isspace():
            pos += 1
        return data[s:pos]

    if tok() != b"P6":
        raise ValueError("not a P6 PPM")
    w, h, mx = int(tok()), int(tok()), int(tok())
    pos += 1
    if mx != 255:
        raise ValueError("only 8-bit PPM supported")
    px = data[pos:pos + w * h * 3]
    if len(px) != w * h * 3:
        raise ValueError("truncated PPM")
    return w, h, px


def stats(w, h, px):
    cnt = Counter(zip(px[0::3], px[1::3], px[2::3]))
    top = cnt.most_common(1)[0][1]
    return {"w": w, "h": h, "distinct": len(cnt), "nonblank": 1 - top / (w * h), "sha": hashlib.sha256(px).hexdigest()}


def diff(a, b):
    if a[:2] != b[:2]:
        raise ValueError("size mismatch")
    pa, pb = a[2], b[2]
    n = sum(x != y for x, y in zip(zip(pa[0::3], pa[1::3], pa[2::3]), zip(pb[0::3], pb[1::3], pb[2::3])))
    return n, n / (a[0] * a[1])


def write_png(path, w, h, px):
    raw = b"".join(b"\x00" + px[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def analyze(files, png_dir=None):
    imgs = []
    for f in files:
        img = read_ppm(f)
        s = stats(*img)
        print(f"screen {s['w']}x{s['h']} distinct_colors={s['distinct']} nonblank_ratio={s['nonblank']:.4f} sha256={s['sha']} file={f}", flush=True)
        if png_dir:
            Path(png_dir).mkdir(parents=True, exist_ok=True)
            write_png(Path(png_dir) / (Path(f).stem + ".png"), *img)
        imgs.append(img)
    if len(imgs) >= 2:
        n, r = diff(imgs[0], imgs[1])
        print(f"frame-diff {files[0]} {files[1]} changed_pixels={n} changed_ratio={r:.4f}", flush=True)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--png-dir")
    a = ap.parse_args(argv)
    try:
        analyze(a.files, a.png_dir)
    except (OSError, ValueError) as e:
        print(f"screencheck: error: {e}")
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
