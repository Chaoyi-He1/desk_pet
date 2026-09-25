#!/usr/bin/env python3
"""Build assets/ships/<ship>/ from the local official sources listed in tools/ships.json.

  python3 tools/build_ships.py [--ship belfast] [--no-paint] [--no-sd] [--no-voice] [--jobs 6]

For each ship:
  ship.ini                  display name, default voice table, oath skins
  skins/<num>-<name>.png    painting, cropped to the character and scaled to <= 1200 px tall
  skins/Q版-<num>-<name>/   chibi frame sequences + meta.ini (tools/spine/render38.py)
  skins/动态-<num>-<name>/  dynamic painting frames (assets/official/dynamic/, tools/spine export_painting)
  skins/L2D-<num>-<name>/   Live2D frames (assets/official/l2d_skins/, tools/live2d)
  voices.tsv                official lines from the wiki: skin num, key, index, oath, zh, jp
Sources: assets/official/*.png (tools/fetch_paintings.py) and
assets/official/game/unpacked/char/ (tools/extract_from_device.py). Everything written
here is copyrighted game content and stays out of git.
"""
import argparse
import csv
import json
import os
import re
import sys
from concurrent.futures import ProcessPoolExecutor, as_completed

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tools", "spine"))
OFFICIAL = os.path.join(ROOT, "assets", "official")
CHAR = os.path.join(OFFICIAL, "game", "unpacked", "char")
DYN = os.path.join(OFFICIAL, "dynamic")
L2D = os.path.join(OFFICIAL, "l2d_skins")
# Animated paintings stand still, are sized by their picture height, and play their long
# main-screen motions only now and then.
PAINTING_META = {"kind": "painting", "walk": "0", "size_ratio": "1.0", "idle_min_ms": "30000", "idle_max_ms": "60000"}
OUT = os.path.join(ROOT, "assets", "ships")
MAX_H = 1200
CHAR_PX = 480  # idle chibi height in exported pixels: ~1:1 on a Retina screen at the default size


def safe(name):
    return re.sub(r'[<>:"/\\|?*]', "_", name).strip()


def build_painting(src, dst):
    img = Image.open(src).convert("RGBA")
    a = np.asarray(img)[:, :, 3]
    ys, xs = np.nonzero(a > 8)
    if len(xs):
        m = 6
        img = img.crop((max(0, xs.min() - m), max(0, ys.min() - m), min(img.width, xs.max() + 1 + m), min(img.height, ys.max() + 1 + m)))
    if img.height > MAX_H:
        img = img.resize((max(1, round(img.width * MAX_H / img.height)), MAX_H), Image.LANCZOS)
    img.save(dst, optimize=True)
    return img.size


def quantize(folder):
    """256-colour palette PNGs via pngquant (visually lossless for these sprites, ~1/3 the size)."""
    import shutil
    import subprocess
    exe = shutil.which("pngquant")
    if not exe:
        return "pngquant not found; frames left as full-colour PNG"
    files = [os.path.join(dp, f) for dp, _, fs in os.walk(folder) for f in fs if f.endswith(".png")]
    for i in range(0, len(files), 200):
        subprocess.run([exe, "--quality=80-98", "--speed=1", "--strip", "--skip-if-larger", "--force", "--ext", ".png",
                        *files[i:i + 200]], check=False)
    return None


def build_sd(model, dst):
    import render38
    lines = []
    w, h = render38.export(os.path.join(CHAR, model), dst, char_px=CHAR_PX, log=lines.append)
    note = quantize(dst)
    if note:
        lines.append(note)
    return model, w, h, lines


def copy_animated(src, dst):
    """Copy a prepared frame-sequence folder and make sure its meta.ini marks it as a painting."""
    import shutil
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst, ignore=shutil.ignore_patterns("_*", ".*"))
    meta_path = os.path.join(dst, "meta.ini")
    lines = open(meta_path, encoding="utf-8").read().splitlines()
    keys = {l.split("=", 1)[0].strip() for l in lines if "=" in l}
    lines += [f"{k}={v}" for k, v in PAINTING_META.items() if k not in keys]
    open(meta_path, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    import make_bpf  # big painting frames are streamed by the app: use the fast frame format
    make_bpf.convert_skin(dst)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ship", action="append", help="only these ships (repeatable)")
    ap.add_argument("--no-paint", action="store_true")
    ap.add_argument("--no-sd", action="store_true", help="skip rendering chibi (Q版) skins")
    ap.add_argument("--no-voice", action="store_true")
    ap.add_argument("--only-animated", action="store_true", help="only copy dynamic-painting and Live2D skins")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 2) - 2))
    a = ap.parse_args()
    ships = {k: v for k, v in json.load(open(os.path.join(ROOT, "tools", "ships.json"), encoding="utf-8")).items()
             if not k.startswith("_") and (not a.ship or k in a.ship)}

    sd_jobs = []
    for key, ship in ships.items():
        base = os.path.join(OUT, key)
        skins_dir = os.path.join(base, "skins")
        os.makedirs(skins_dir, exist_ok=True)
        oath = [s["num"] for s in ship["skins"] if s.get("oath")]
        with open(os.path.join(base, "ship.ini"), "w", encoding="utf-8") as f:
            f.write("; written by tools/build_ships.py\n[ship]\n")
            f.write(f"name={ship['name']}\ndefault_voice={ship['default_voice']}\noath_skins={','.join(oath)}\n")
            # skin num -> voice table num, for skins that borrow another skin's lines
            for s in ship["skins"]:
                if s.get("voice"):
                    f.write(f"voice_{s['num']}={s['voice']}\n")
        for s in ship["skins"]:
            label = f"{s['num']}-{safe(s['name'])}"
            if not a.no_paint and not a.only_animated and s.get("painting"):
                src = os.path.join(OFFICIAL, s["painting"])
                if os.path.exists(src):
                    size = build_painting(src, os.path.join(skins_dir, label + ".png"))
                    print(f"{key:9s} painting {label:28s} {size[0]}x{size[1]}")
                else:
                    print(f"{key:9s} painting {label:28s} MISSING {s['painting']} (run tools/fetch_paintings.py)")
            for key_name, root, prefix in (("dyn", DYN, "动态-"), ("l2d", L2D, "L2D-")):
                if s.get(key_name):  # prepared frames: copied even with --no-sd (that only skips chibi renders)
                    src = os.path.join(root, s[key_name])
                    if os.path.isfile(os.path.join(src, "meta.ini")):
                        copy_animated(src, os.path.join(skins_dir, prefix + label))
                        print(f"{key:9s} {key_name:8s} {label:28s} <- {s[key_name]}")
                    else:
                        print(f"{key:9s} {key_name:8s} {label:28s} MISSING {src}")
            if a.only_animated:
                continue
            if not a.no_sd and s.get("sd"):
                if os.path.isdir(os.path.join(CHAR, s["sd"])):
                    sd_jobs.append((key, s["sd"], os.path.join(skins_dir, "Q版-" + label)))
                else:
                    print(f"{key:9s} chibi    {label:28s} MISSING model {s['sd']} (run tools/extract_from_device.py)")

        if not a.no_voice:
            import fetch_voice as fv
            html = fv.request(fv.page_url(ship["wiki_page"]), throttle=fv.Throttle())[1].decode("utf-8")
            rows = fv.parse(html)
            by_title = {s["wiki"]: s["num"] for s in ship["skins"] if "wiki" in s}
            n = 0
            with open(os.path.join(base, "voices.tsv"), "w", encoding="utf-8", newline="") as f:
                w = csv.writer(f, delimiter="\t", lineterminator="\n")
                w.writerow(["skin", "key", "index", "oath", "zh", "jp"])
                for r in rows:
                    num = by_title.get(r["title"])
                    if num is None:
                        continue
                    w.writerow([num, r["key"], r["index"], r["oath"], r["zh"], r["jp"]])
                    n += 1
            unknown = sorted({r["title"] for r in rows} - set(by_title))
            print(f"{key:9s} voices   {n} lines" + (f"; unmapped wiki tables: {unknown}" if unknown else ""))

    if sd_jobs:
        print(f"rendering {len(sd_jobs)} chibi models with {a.jobs} workers ...")
        with ProcessPoolExecutor(max_workers=a.jobs) as ex:
            futs = {ex.submit(build_sd, model, dst): (key, model) for key, model, dst in sd_jobs}
            for fut in as_completed(futs):
                key, model = futs[fut]
                try:
                    _, w, h, lines = fut.result()
                    print(f"{key:9s} chibi    {model:18s} {w}x{h}")
                    for l in lines:
                        print("            " + l)
                except Exception as e:  # keep going; report at the end
                    print(f"{key:9s} chibi    {model:18s} FAILED: {e!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
