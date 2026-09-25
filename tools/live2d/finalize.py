#!/usr/bin/env python3
"""Turn raw Live2D captures (assets/official/l2dframes/<model>/<state>/NNN.png, 2048x2048)
into a pet skin folder under assets/official/l2d_skins/<model>/.

  python3 tools/live2d/finalize.py [MODEL ...]

All frames share one crop (the union of visible pixels, leaving out the states listed
under "clip" in models.json) and are scaled so that it is PICTURE_PX tall, then quantised
with pngquant. States whose motion is under "skip" are left out. The first idle frame
(magnified by jobs.py --still when that was rendered) is also saved as assets/official/stills/<model>.png: a
background-free static painting for skins whose official picture has scenery baked in. meta.ini marks the skin as a stationary
painting (kind=painting, walk=0, size_ratio=1: the pet size is the picture height) and
spaces out the long main-screen motions (idle_min_ms / idle_max_ms).
"""
import os
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from jobs import STATES, config  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
RAW = os.path.join(ROOT, "assets", "official", "l2dframes")
OUT = os.path.join(ROOT, "assets", "official", "l2d_skins")
STILLS = os.path.join(ROOT, "assets", "official", "stills")
PICTURE_PX = 720
FPS = 8


def frames(model, skip=()):
    d = os.path.join(RAW, model)
    motion = dict(STATES)
    out = {}
    for state in sorted(os.listdir(d)):
        p = os.path.join(d, state)
        if os.path.isdir(p) and state != "census" and motion.get(state) not in skip:
            out[state] = [os.path.join(p, f) for f in sorted(os.listdir(p)) if f.endswith(".png")]
    return out


def finalize(model):
    cfg = config().get(model, {})
    fr = frames(model, cfg.get("skip", []))
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    for state, fs in fr.items():
        if state in cfg.get("clip", []):
            continue
        for f in fs[::2]:
            bb = Image.open(f).getchannel("A").point(lambda v: 255 if v > 6 else 0).getbbox()
            if bb:
                x0, y0, x1, y1 = min(x0, bb[0]), min(y0, bb[1]), max(x1, bb[2]), max(y1, bb[3])
    m = 8
    box = (max(0, x0 - m), max(0, y0 - m), x1 + m, y1 + m)
    s = PICTURE_PX / (box[3] - box[1])
    W, H = round((box[2] - box[0]) * s), round((box[3] - box[1]) * s)
    dst = os.path.join(OUT, model)
    shutil.rmtree(dst, ignore_errors=True)
    head_top, bottom = None, None
    for state, fs in fr.items():
        os.makedirs(os.path.join(dst, state), exist_ok=True)
        for i, f in enumerate(fs):
            im = Image.open(f).convert("RGBA").crop(box).convert("RGBa").resize((W, H), Image.LANCZOS).convert("RGBA")
            if state == "idle":
                a = np.asarray(im)[..., 3]
                rows = np.nonzero(a.max(axis=1) > 16)[0]
                if len(rows):
                    head_top = rows.min() if head_top is None else min(head_top, rows.min())
                    bottom = rows.max() if bottom is None else max(bottom, rows.max())
            im.save(os.path.join(dst, state, f"{i:03d}.png"))
    hi = os.path.join(RAW, "_stills", model, "still", "000.png")  # jobs.py --still, if rendered
    still = Image.open(hi if os.path.exists(hi) else fr["idle"][0]).convert("RGBA")
    bb = still.getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    os.makedirs(STILLS, exist_ok=True)
    (still.crop(bb) if bb else still).save(os.path.join(STILLS, model + ".png"), optimize=True)
    pq = shutil.which("pngquant")
    if pq:
        files = [os.path.join(dp, f) for dp, _, fs in os.walk(dst) for f in fs if f.endswith(".png")]
        for k in range(0, len(files), 200):
            subprocess.run([pq, "--quality=80-98", "--speed=1", "--strip", "--skip-if-larger", "--force", "--ext", ".png",
                            *files[k:k + 200]], check=False)
    with open(os.path.join(dst, "meta.ini"), "w") as f:
        f.write("; written by tools/live2d/finalize.py\n[sequence]\n")
        f.write(f"fps={FPS}\nwidth={W}\nheight={H}\n")
        f.write(f"ground={H - 1 - int(bottom or H - 1)}\nhead_top={int(head_top or 0)}\nchar_height={H}\n")
        f.write("head_fraction=0.25\nkind=painting\nwalk=0\nsize_ratio=1.0\nidle_min_ms=30000\nidle_max_ms=60000\n")
        f.write(f"model={model}\n")
    n = sum(len(v) for v in fr.values())
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(dst) for f in fs)
    print(f"{model}: {W}x{H}, {n} frames, {size / 1e6:.1f} MB, states {', '.join(f'{k}={len(v)}' for k, v in fr.items())}")


if __name__ == "__main__":
    for m in (sys.argv[1:] or sorted(d for d in os.listdir(RAW) if os.path.isdir(os.path.join(RAW, d)))):
        finalize(m)
