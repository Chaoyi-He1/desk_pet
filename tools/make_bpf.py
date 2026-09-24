#!/usr/bin/env python3
"""Convert the PNG frames of animated-painting skins (Live2D, dynamic paintings: meta.ini
kind=painting) to .bpf frames (see src/core/frames.h) and delete the PNGs.

  python3 tools/make_bpf.py SKIN_DIR [SKIN_DIR ...]
  python3 tools/make_bpf.py --all          every painting-kind skin under assets/ships

.bpf = 256-colour palette (premultiplied BGRA) + LZ4-HC compressed indices of the visible
box. About the size of the PNG, ~10x faster to decode, so the pet can stream these big
frames from disk instead of keeping hundreds of them in memory.
"""
import glob
import os
import struct
import sys

import lz4.block
import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def encode(png_path):
    im = Image.open(png_path)
    if im.mode != "P":
        im = im.convert("RGBA").quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
    idx = np.asarray(im, dtype=np.uint8)
    rgba = np.asarray(im.convert("RGBA"), dtype=np.uint32)
    H, W = idx.shape
    # palette from the pixels themselves (handles tRNS / per-index alpha uniformly)
    pal = np.zeros(256, dtype=np.uint32)
    flat_i = idx.reshape(-1)
    flat_c = rgba.reshape(-1, 4)
    first = {}
    uniq, pos = np.unique(flat_i, return_index=True)
    for u, p in zip(uniq, pos):
        r, g, b, a = (int(v) for v in flat_c[p])
        pr, pg, pb = ((c * a + 127) // 255 for c in (r, g, b))
        pal[u] = pb | (pg << 8) | (pr << 16) | (a << 24)
    count = int(uniq.max()) + 1
    alpha = (pal[idx] >> 24) & 255
    ys, xs = np.nonzero(alpha > 0)
    if len(xs):
        x0, x1, y0, y1 = int(xs.min()), int(xs.max()) + 1, int(ys.min()), int(ys.max()) + 1
    else:
        x0 = y0 = 0
        x1 = y1 = 1
    box = np.ascontiguousarray(idx[y0:y1, x0:x1]).tobytes()
    comp = lz4.block.compress(box, mode="high_compression", compression=12, store_size=False)
    head = b"BPF1" + struct.pack("<7H", W, H, x0, y0, x1 - x0, y1 - y0, count)
    return head + pal[:count].astype("<u4").tobytes() + struct.pack("<I", len(comp)) + comp


def convert_skin(d):
    files = sorted(glob.glob(os.path.join(d, "*", "*.png")))
    before = sum(os.path.getsize(f) for f in files)
    after = 0
    for f in files:
        data = encode(f)
        with open(f[:-4] + ".bpf", "wb") as out:
            out.write(data)
        after += len(data)
        os.remove(f)
    return len(files), before, after


def is_painting(d):
    meta = os.path.join(d, "meta.ini")
    return os.path.isfile(meta) and "kind=painting" in open(meta, encoding="utf-8").read()


def main():
    dirs = sys.argv[1:]
    if dirs == ["--all"]:
        dirs = [d for d in sorted(glob.glob(os.path.join(ROOT, "assets", "ships", "*", "skins", "*"))) if is_painting(d)]
    for d in dirs:
        n, b, a = convert_skin(d)
        if n:
            print(f"{os.path.relpath(d, ROOT)}: {n} frames, {b / 1e6:.1f} MB png -> {a / 1e6:.1f} MB bpf")


if __name__ == "__main__":
    main()
