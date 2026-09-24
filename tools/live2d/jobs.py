#!/usr/bin/env python3
"""Print the render.html URL that captures the pet states of Live2D models.

  python3 tools/live2d/jobs.py [MODEL ...] [--states react_special,blink_main2]

Open the URL (served by tools/live2d/server.py) and wait for "ALL DONE" in
assets/official/l2dframes/_log.txt, then run finalize.py. Pet states come from the
game motions below; models.json lists the parts to hide and the motions to skip.
"""
import argparse
import json
import os
import urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
MODELS = os.path.join(ROOT, "assets", "official", "live2d")
CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "models.json")
FPS = 8

# pet state folder <- game motion. blink_* are the idle actions picked at random,
# react = body tap, react_head = head pat, react_special = the "special touch".
STATES = [
    ("idle", "idle"),
    ("blink_main", "main_1"),
    ("blink_main2", "main_2"),
    ("blink_main3", "main_3"),
    ("blink_main4", "main_4"),
    ("react", "touch_body"),
    ("react_head", "touch_head"),
    ("react_special", "touch_special"),
]


def config():
    with open(CONFIG, encoding="utf-8") as f:
        return {k: v for k, v in json.load(f).items() if not k.startswith("_")}


def duration(model, motion):
    p = os.path.join(MODELS, model, "motions", motion + ".motion3.json")
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8") as f:
        return round(json.load(f)["Meta"]["Duration"], 3)


def job(model, cfg, only=None):
    states = []
    for state, motion in STATES:
        d = duration(model, motion)
        if d is None or motion in cfg.get("skip", []) or state == "idle":
            continue
        if only and state not in only:
            continue
        states.append({"state": state, "motion": motion, "duration": d})
    return {"model": f"/assets/official/live2d/{model}/{model}.model3.json", "out": model, "fps": FPS,
            "hideParts": cfg.get("hide", []), "hideDrawables": cfg.get("hide_meshes", []), "idle": {"motion": "idle", "duration": duration(model, "idle")},
            "states": ([] if only and "idle" not in only else [{"state": "idle", "motion": "idle",
                                                               "duration": duration(model, "idle")}]) + states}


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("models", nargs="*")
    ap.add_argument("--states", help="comma-separated pet states to render (default: all)")
    ap.add_argument("--port", type=int, default=8765)
    a = ap.parse_args()
    cfg = config()
    only = set(a.states.split(",")) if a.states else None
    jobs = [job(m, cfg.get(m, {}), only) for m in (a.models or sorted(cfg))]
    print(f"http://localhost:{a.port}/tools/live2d/render.html?jobs=" + urllib.parse.quote(json.dumps(jobs)))
