#!/usr/bin/env python3
"""Print the render.html URL that captures the pet states of Live2D models.

  python3 tools/live2d/jobs.py [MODEL ...] [--states react_special,blink_main2] [--json tools/live2d/.jobs.json]
  python3 tools/live2d/jobs.py MODEL ... --still --json tools/live2d/.jobs.json   (after the idle capture)

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
    ("blink_main5", "main_5"),
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
            "hideParts": cfg.get("hide", []), "hideDrawables": cfg.get("hide_meshes", []),
            "pinParams": cfg.get("pin", {}), "idle": {"motion": "idle", "duration": duration(model, "idle")},
            "states": ([] if only and "idle" not in only else [{"state": "idle", "motion": "idle",
                                                               "duration": duration(model, "idle")}]) + states}


def still_job(model, cfg):
    """One idle frame magnified to fill the canvas (visible box taken from the idle capture):
    the background-free static painting that finalize.py saves to assets/official/stills/."""
    from PIL import Image
    first = os.path.join(ROOT, "assets", "official", "l2dframes", model, "idle", "000.png")
    x0, y0, x1, y1 = Image.open(first).getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    zoom = min(0.96 * 2048 / (x1 - x0), 0.96 * 2048 / (y1 - y0))
    j = job(model, cfg, {"idle"})
    j["out"] = f"_stills/{model}"
    j["states"] = [{"state": "still", "motion": "idle", "duration": 1.0 / FPS}]
    j["view"] = {"zoom": round(zoom, 4), "cx": (x0 + x1) / 2, "cy": (y0 + y1) / 2}
    return j


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("models", nargs="*")
    ap.add_argument("--states", help="comma-separated pet states to render (default: all)")
    ap.add_argument("--still", action="store_true", help="render high-resolution stills (needs an idle capture)")
    ap.add_argument("--port", type=int, default=8765)
    ap.add_argument("--json", metavar="FILE", help="write the job list to FILE (e.g. tools/live2d/.jobs.json, which "
                                                   "render.html?jobsFile=.jobs.json loads) instead of printing a URL")
    a = ap.parse_args()
    cfg = config()
    only = set(a.states.split(",")) if a.states else None
    make = (lambda m: still_job(m, cfg.get(m, {}))) if a.still else (lambda m: job(m, cfg.get(m, {}), only))
    jobs = [make(m) for m in (a.models or sorted(cfg))]
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            json.dump(jobs, f, ensure_ascii=False)
        print(f"http://localhost:{a.port}/tools/live2d/render.html?jobsFile={os.path.basename(a.json)}")
    else:
        print(f"http://localhost:{a.port}/tools/live2d/render.html?jobs=" + urllib.parse.quote(json.dumps(jobs)))
