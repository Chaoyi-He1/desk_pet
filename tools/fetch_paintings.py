#!/usr/bin/env python3
"""Download the official paintings named in tools/ships.json into assets/official/.

Source: github.com/Fernando2603/AzurLane (assets extracted from the EN client). File names
in ships.json encode the skin id and whether the background-free variant is used
(..._painting_n.png) or the regular one (..._painting.png). Existing files are kept.
The art is © Manjuu / Yongshi / Yostar: personal use only, do not redistribute.
"""
import json
import os
import re
import sys
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = "https://raw.githubusercontent.com/Fernando2603/AzurLane/main/images/skin"


def main():
    ships = json.load(open(os.path.join(ROOT, "tools", "ships.json"), encoding="utf-8"))
    out = os.path.join(ROOT, "assets", "official")
    os.makedirs(out, exist_ok=True)
    for key, ship in ships.items():
        if key.startswith("_"):
            continue
        for s in ship["skins"]:
            p = s.get("painting")
            if not p or "id" not in s or "/" in p:
                continue
            dst = os.path.join(out, p)
            if os.path.exists(dst) and os.path.getsize(dst) > 0:
                continue
            kind = "painting_n" if re.search(r"_painting_n\.png$", p) else "painting"
            url = f"{BASE}/{s['id']}/{kind}.png"
            with urllib.request.urlopen(urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"}), timeout=180) as r:
                data = r.read()
            open(dst, "wb").write(data)
            print(f"{p}: {len(data) / 1e6:.2f} MB")
            time.sleep(0.3)
    return 0


if __name__ == "__main__":
    sys.exit(main())
