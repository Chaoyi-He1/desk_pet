#!/usr/bin/env python3
"""Turn the raw official paintings in assets/official/ into ready-to-use skins.

Each skin is cropped to its opaque bounding box (plus a small margin) and downscaled so
the tallest side is at most MAX_H pixels; the app displays skins at min(config height,
2 x source height), so 1200 px is more than enough for a 320-640 px pet.
"""
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "assets", "official")
DST = os.path.join(ROOT, "assets", "skins")
MAX_H = 1200
MARGIN = 6

# (source file, output stem). Prefer painting_n (no special background) when it exists.
SKINS = [
    ("202129_retrofit_painting_n.png", "01-改造"),
    ("202120_default_painting.png", "02-默认女仆装"),
    ("202121_iridescent-rosa_painting.png", "03-彩云之玫瑰 旗袍"),
    ("202122_serene-steel_painting.png", "04-Serene Steel 礼服"),
    ("202123_noble-attendant_painting.png", "05-Noble Attendant 晚礼服"),
    ("202124_shopping-casual_painting.png", "06-便服 逛街"),
    ("202125_piping-hot-perfection_painting.png", "07-完美的代理店长 披萨店"),
    ("202126_folded-fascination_painting.png", "08-倾城之华扇 和服"),
    ("202127_blissful-service_painting_n.png", "09-泳池 坐姿"),
    ("202128_pledge-of-claddagh_painting.png", "10-婚纱"),
]
CHIBIS = [
    ("202129_retrofit_chibi.png", "Q版-01-改造"),
    ("202120_default_chibi.png", "Q版-02-默认"),
    ("202121_iridescent-rosa_chibi.png", "Q版-03-旗袍"),
    ("202122_serene-steel_chibi.png", "Q版-04-礼服"),
    ("202123_noble-attendant_chibi.png", "Q版-05-晚礼服"),
    ("202124_shopping-casual_chibi.png", "Q版-06-便服"),
    ("202125_piping-hot-perfection_chibi.png", "Q版-07-披萨店"),
    ("202126_folded-fascination_chibi.png", "Q版-08-和服"),
    ("202127_blissful-service_chibi.png", "Q版-09-泳池"),
    ("202128_pledge-of-claddagh_chibi.png", "Q版-10-婚纱"),
]


def crop_alpha(img, margin):
    a = np.array(img)[:, :, 3]
    ys, xs = np.nonzero(a > 8)
    if len(xs) == 0:
        return img
    x0, x1 = max(0, xs.min() - margin), min(img.width, xs.max() + 1 + margin)
    y0, y1 = max(0, ys.min() - margin), min(img.height, ys.max() + 1 + margin)
    return img.crop((x0, y0, x1, y1))


def process(src, stem, max_h):
    img = Image.open(os.path.join(SRC, src)).convert("RGBA")
    img = crop_alpha(img, MARGIN)
    if img.height > max_h:
        s = max_h / img.height
        img = img.resize((max(1, round(img.width * s)), max_h), Image.LANCZOS)
    out = os.path.join(DST, stem + ".png")
    img.save(out, optimize=True)
    print(f"{stem:32s} {img.width}x{img.height}  {os.path.getsize(out)//1024} KB")


def main():
    os.makedirs(DST, exist_ok=True)
    for src, stem in SKINS:
        process(src, stem, MAX_H)
    for src, stem in CHIBIS:
        process(src, stem, 10_000)  # keep chibi sprites at native size


if __name__ == "__main__":
    main()
