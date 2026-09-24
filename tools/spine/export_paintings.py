#!/usr/bin/env python3
"""Export the dynamic paintings listed in tools/spine/paintings.json to
assets/official/dynamic/<name>/ (frame folders + meta.ini), in parallel.

  python3 tools/spine/export_paintings.py [NAME ...]
"""
import json
import os
import re
import shutil
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(ROOT, "assets", "official", "dynamic")


def export_one(name, cfg):
    sys.path.insert(0, HERE)
    import render38
    skel = os.path.join(ROOT, cfg["skel"])
    probe = render38.open_painting(skel)
    parts = probe.parts if hasattr(probe, "parts") else [probe]
    names = [s.name for p in parts for s in p.sk.slots]
    hide = {n for n in names for pat in cfg.get("hide", []) if re.search(pat, n)}
    dst = os.path.join(OUT, name)
    shutil.rmtree(dst, ignore_errors=True)
    lines = []
    w, h = render38.export_painting(skel, dst, expressions=cfg.get("expressions"), hide_slots=hide, log=lines.append,
                                    ground_patch=cfg.get("ground_patch"))
    size = sum(os.path.getsize(os.path.join(dp, f)) for dp, _, fs in os.walk(dst) for f in fs)
    counts = {d: len(os.listdir(os.path.join(dst, d))) for d in sorted(os.listdir(dst)) if os.path.isdir(os.path.join(dst, d))}
    return name, w, h, size, counts, len(hide), lines


def main():
    cfg = json.load(open(os.path.join(HERE, "paintings.json"), encoding="utf-8"))
    todo = [n for n in cfg if not n.startswith("_") and (not sys.argv[1:] or n in sys.argv[1:])]
    with ProcessPoolExecutor(max_workers=min(4, len(todo))) as ex:
        futs = [ex.submit(export_one, n, cfg[n]) for n in todo]
        for f in as_completed(futs):
            name, w, h, size, counts, nh, lines = f.result()
            print(f"{name}: {w}x{h}, {size / 1e6:.1f} MB, frames {counts}, {nh} scenery slots hidden")
            for l in lines:
                print("    " + l)


if __name__ == "__main__":
    main()
