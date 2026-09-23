#!/usr/bin/env python3
"""Pre-render a Spine 3.8 SD (chibi) model into PNG frame sequences for BelfastPet.

  python3 tools/spine/render38.py MODEL_DIR OUT_DIR [--fps 12] [--scale 1.0] [--map state=anim ...]

MODEL_DIR holds <name>.skel, <name>.atlas and the atlas page PNG(s) (as unpacked by
tools/extract_from_device.py). OUT_DIR receives one folder per pet state (frames
000.png, 001.png ...) and meta.ini. All frames share one canvas and one ground line, so
the pet does not jump between states. Frames face right; the app mirrors them.

Without --map, each state picks the first suitable animation from STATE_CANDIDATES:
showy skins whose idle spans a whole stage fall back to a calmer idle, and random idle
actions that are much bigger than the character are left out.
"""
import argparse
import ctypes
import math
import os
import subprocess
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from skel38 import read_atlas, read_skel  # noqa: E402
from pose38 import Pose  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))

# state folder -> candidate animations in order of preference. Folders "<state>_<suffix>"
# are extra variants of <state>: blink_* are random idle actions, react_head is the head pat.
STATE_CANDIDATES = [
    ("idle", ["stand2", "normal", "stand"]),
    ("walk", ["walk", "move"]),
    ("drag", ["tuozhuai", "tuozhuai2"]),
    ("fall", ["tuozhuai2", "tuozhuai"]),
    ("react", ["touch", "motou"]),
    ("react_head", ["motou", "touch"]),
    ("sleep", ["sleep"]),
    ("land", ["yun"]),
    ("blink_sit", ["sit"]),
    ("blink_dance", ["dance"]),
    ("blink_victory", ["victory"]),
]
REQUIRED = {"idle", "walk", "drag", "fall", "react", "sleep"}
MAX_AREA_RATIO = 1.8   # vs. the smallest idle candidate; larger animations are skipped
MAX_ONESHOT_SEC = 5.0  # optional one-shot actions longer than this are skipped (RAM, CPU)


def load_raster():
    src = os.path.join(HERE, "raster.c")
    lib = os.path.join(HERE, "_raster.so")
    if not os.path.exists(lib) or os.path.getmtime(lib) < os.path.getmtime(src):
        subprocess.check_call(["cc", "-O3", "-shared", "-fPIC", "-o", lib, src, "-lm"])
    so = ctypes.CDLL(lib)
    fp = ctypes.POINTER(ctypes.c_float)
    ip = ctypes.POINTER(ctypes.c_int)
    so.draw_tris.argtypes = [fp, ctypes.c_int, ctypes.c_int, fp, ctypes.c_int, ctypes.c_int, fp, fp, ip, ctypes.c_int,
                             ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_float, ctypes.c_int]
    return so


def page_to_pixel(region, cx, cy):
    """Content pixel (cx right, cy down, relative to the stripped image) -> page pixel."""
    if region.rotate:  # libGDX packs rotated regions 90 degrees counter-clockwise
        return region.x + cy, region.y + (region.width - cx)
    return region.x + cx, region.y + cy


def prepare_attachments(sk, regions):
    for _, table in sk.skins:
        for atts in table.values():
            for a in atts.values():
                if a.type not in ("region", "mesh"):
                    continue
                r = regions[a.path]
                a.page = r.page
                ow, oh = r.orig_w or r.width, r.orig_h or r.height
                if a.type == "region":
                    lx = -a.width / 2 + r.offset_x / ow * a.width
                    ly = -a.height / 2 + r.offset_y / oh * a.height
                    lx2 = a.width / 2 - (ow - r.offset_x - r.width) / ow * a.width
                    ly2 = a.height / 2 - (oh - r.offset_y - r.height) / oh * a.height
                    lx, lx2, ly, ly2 = lx * a.scale_x, lx2 * a.scale_x, ly * a.scale_y, ly2 * a.scale_y
                    cs, sn = math.cos(math.radians(a.rotation)), math.sin(math.radians(a.rotation))
                    corners = [(lx, ly), (lx, ly2), (lx2, ly2), (lx2, ly)]  # BL, UL, UR, BR
                    a.offset = [(x * cs - y * sn + a.x, x * sn + y * cs + a.y) for x, y in corners]
                    w, h = r.width, r.height
                    a.page_uv = [page_to_pixel(r, 0, h), page_to_pixel(r, 0, 0), page_to_pixel(r, w, 0), page_to_pixel(r, w, h)]
                    a.tris = [0, 1, 2, 2, 3, 0]
                else:
                    top = oh - r.offset_y - r.height
                    a.page_uv = [page_to_pixel(r, a.uvs[i] * ow - r.offset_x, a.uvs[i + 1] * oh - top)
                                 for i in range(0, len(a.uvs), 2)]
                    a.tris = list(a.triangles)


def frame_times(anim, fps):
    n = max(1, int(round(anim.duration * fps)))
    return [i / fps for i in range(n)]


class Model:
    def __init__(self, model_dir):
        self.name = os.path.basename(os.path.normpath(model_dir))
        self.sk = read_skel(open(os.path.join(model_dir, self.name + ".skel"), "rb").read())
        regions, pages = read_atlas(open(os.path.join(model_dir, self.name + ".atlas"), encoding="utf-8").read())
        prepare_attachments(self.sk, regions)
        self.tex = {}
        self.tex_scale = {}
        for p in pages:
            # Prefer a 2x super-resolved page (tools/spine/upscale.py); UVs are scaled to match.
            hi = os.path.join(model_dir, p[:-4] + "@2x.png")
            path, k = (hi, 2.0) if os.path.exists(hi) else (os.path.join(model_dir, p), 1.0)
            img = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32) / 255.0
            img[..., :3] *= img[..., 3:4]  # straight -> premultiplied alpha
            self.tex[p] = np.ascontiguousarray(img)
            self.tex_scale[p] = k
        self.pose = Pose(self.sk)
        self.so = load_raster()

    def anim(self, name):
        return self.sk.animation(name)

    def geometry(self, anim, t):
        """Draw list: ("draw", att, verts, colour, additive) / ("clip", polygon) / ("unclip",)."""
        pose = self.pose
        pose.apply(anim, t)
        out = []
        clip_end = None
        for si in pose.draw_order:
            slot = pose.slots[si]
            att = slot.attachment
            if att is not None and att.type == "clipping" and clip_end is None:
                out.append(("clip", pose.world_vertices(slot)))
                clip_end = att.end_slot
            elif att is not None and att.type in ("region", "mesh"):
                col = [slot.color[k] * att.color[k] for k in range(4)]
                if col[3] > 0:
                    out.append(("draw", att, pose.world_vertices(slot), col, slot.data.blend == 1))
            if clip_end is not None and slot.data.index == clip_end:
                out.append(("unclip",))
                clip_end = None
        if clip_end is not None:
            out.append(("unclip",))
        return out

    def geometry_box(self, anims_times):
        x0 = y0 = math.inf
        x1 = y1 = -math.inf
        for anim, times in anims_times:
            for t in times:
                for item in self.geometry(anim, t):
                    if item[0] == "draw":
                        for x, y in item[2]:
                            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
        return x0, y0, x1, y1

    def render(self, anim, t, box, scale, ss):
        """One frame covering skeleton-space `box` -> straight-alpha RGBA uint8 array (y down)."""
        fp = ctypes.POINTER(ctypes.c_float)
        ip = ctypes.POINTER(ctypes.c_int)
        minx, miny, maxx, maxy = box
        W = max(1, int(math.ceil((maxx - minx) * scale)))
        H = max(1, int(math.ceil((maxy - miny) * scale)))
        base = np.zeros((H * ss, W * ss, 4), dtype=np.float32)
        fb, mask = base, None
        k = scale * ss
        for item in self.geometry(anim, t):
            if item[0] == "clip":
                # Clipped slots go to their own layer, masked by the polygon, then composited;
                # with premultiplied alpha this equals clipping the geometry.
                poly = [((x - minx) * k, (maxy - y) * k) for x, y in item[1]]
                m = Image.new("L", (W * ss, H * ss), 0)
                if len(poly) >= 3:
                    ImageDraw.Draw(m).polygon(poly, fill=255)
                mask = np.asarray(m, dtype=np.float32)[..., None] / 255.0
                fb = np.zeros_like(base)
                continue
            if item[0] == "unclip":
                layer = fb * mask
                base[:] = layer + base * (1.0 - layer[..., 3:4])
                fb, mask = base, None
                continue
            _, att, verts, col, additive = item
            xy = np.array([((x - minx) * k, (maxy - y) * k) for x, y in verts], dtype=np.float32)
            uv = np.array(att.page_uv, dtype=np.float32) * self.tex_scale[att.page]
            tris = np.array(att.tris, dtype=np.int32)
            tx = self.tex[att.page]
            self.so.draw_tris(tx.ctypes.data_as(fp), tx.shape[1], tx.shape[0], fb.ctypes.data_as(fp), W * ss, H * ss,
                              xy.ctypes.data_as(fp), uv.ctypes.data_as(fp), tris.ctypes.data_as(ip), len(tris) // 3,
                              col[0], col[1], col[2], col[3], 1 if additive else 0)
        img = base.reshape(H, ss, W, ss, 4).mean(axis=(1, 3))  # box filter (premultiplied)
        alpha = img[..., 3:4]
        rgb = np.where(alpha > 1e-6, img[..., :3] / np.maximum(alpha, 1e-6), 0)
        return np.clip(np.concatenate([rgb, alpha], axis=2) * 255 + 0.5, 0, 255).astype(np.uint8)

    def visible_box(self, anims_times, step=2):
        """Skeleton-space box of what is actually visible (alpha > 8), from a coarse render."""
        gbox = self.geometry_box(anims_times)
        gx0, gy0, gx1, gy1 = gbox
        if not math.isfinite(gx0):
            return None
        coarse = min(0.25, 400.0 / max(gx1 - gx0, gy1 - gy0, 1))
        vx0 = vy0 = math.inf
        vx1 = vy1 = -math.inf
        for anim, times in anims_times:
            for t in times[::step]:
                img = self.render(anim, t, gbox, coarse, 1)
                ys, xs = np.nonzero(img[..., 3] > 8)
                if len(xs) == 0:
                    continue
                vx0 = min(vx0, gx0 + xs.min() / coarse)
                vx1 = max(vx1, gx0 + (xs.max() + 1) / coarse)
                vy1 = max(vy1, gy1 - ys.min() / coarse)
                vy0 = min(vy0, gy1 - (ys.max() + 1) / coarse)
        if not math.isfinite(vx0):
            return None
        return vx0, vy0, vx1, vy1


def area(box):
    return max(0.0, box[2] - box[0]) * max(0.0, box[3] - box[1])


def choose_states(model, fps, log=print):
    """Picks an animation per state (see STATE_CANDIDATES). Returns {state: anim}."""
    boxes = {}

    def box_of(name):
        if name not in boxes:
            anim = model.anim(name)
            boxes[name] = model.visible_box([(anim, frame_times(anim, min(fps, 6)))]) if anim else None
        return boxes[name]

    idle_boxes = [(n, box_of(n)) for n in ("stand2", "normal", "stand") if model.anim(n) and box_of(n)]
    if not idle_boxes:
        raise SystemExit(f"{model.name}: no idle animation")
    ref = min(area(b) for _, b in idle_boxes)
    chosen = {}
    for state, cands in STATE_CANDIDATES:
        ok = []
        for c in cands:
            anim = model.anim(c)
            b = box_of(c) if anim else None
            if b is None:
                continue
            ratio = area(b) / ref
            if ratio > MAX_AREA_RATIO:
                log(f"  skip {c:10s} for {state:13s}: {ratio:.1f}x the character")
                continue
            if state not in REQUIRED and anim.duration > MAX_ONESHOT_SEC:
                log(f"  skip {c:10s} for {state:13s}: {anim.duration:.1f}s long")
                continue
            ok.append(anim)
        if ok:
            # idle loops forever: the shortest calm loop keeps the frame count (RAM) down
            chosen[state] = min(ok, key=lambda a: a.duration) if state == "idle" else ok[0]
        elif state in REQUIRED:
            # nothing suitable: take the smallest candidate rather than dropping a state
            sized = [(area(box_of(c)), c) for c in cands if model.anim(c) and box_of(c)]
            if sized:
                chosen[state] = model.anim(min(sized)[1])
    return chosen


def export(model_dir, out_dir, fps=10.0, scale=None, ss=3, mapping=None, char_px=480, log=print):
    """`scale` px per skeleton unit; by default chosen so the idle character is `char_px` tall,
    which keeps every model equally sharp whatever its skeleton units."""
    m = Model(model_dir)
    if mapping:
        chosen = {s: m.anim(a) for s, a in mapping.items() if m.anim(a)}
    else:
        chosen = choose_states(m, fps, log)
    plan = {s: (a, frame_times(a, fps)) for s, a in chosen.items()}
    if scale is None:
        idle_anim, idle_times = plan["idle"]
        ib = m.visible_box([(idle_anim, idle_times[::3] or idle_times)], step=1)
        scale = char_px / max(1.0, ib[3] - ib[1])
    vb = m.visible_box([(a, t) for a, t in plan.values()])
    pad = 6 / scale
    box = (vb[0] - pad, min(vb[1], 0.0) - pad, vb[2] + pad, vb[3] + pad)
    W = int(math.ceil((box[2] - box[0]) * scale))
    H = int(math.ceil((box[3] - box[1]) * scale))
    log(f"{m.name}: canvas {W}x{H}; " + ", ".join(f"{s}={a.name}" for s, (a, _) in plan.items()))

    head_top, heights = None, []
    for state, (anim, times) in plan.items():
        d = os.path.join(out_dir, state)
        os.makedirs(d, exist_ok=True)
        for f in os.listdir(d):
            if f.endswith(".png"):
                os.remove(os.path.join(d, f))
        for i, t in enumerate(times):
            img = m.render(anim, t, box, scale, ss)
            if state == "idle":
                ys = np.nonzero(img[..., 3].max(axis=1) > 16)[0]
                if len(ys):
                    head_top = ys.min() if head_top is None else min(head_top, ys.min())
                    heights.append(ys.max() - ys.min() + 1)
            Image.fromarray(img, "RGBA").save(os.path.join(d, f"{i:03d}.png"), optimize=True)
    with open(os.path.join(out_dir, "meta.ini"), "w") as f:
        f.write("; written by tools/spine/render38.py\n[sequence]\n")
        f.write(f"fps={fps:g}\nwidth={W}\nheight={H}\n")
        f.write(f"ground={int(round((0 - box[1]) * scale))}\n")        # canvas bottom -> feet, px
        f.write(f"head_top={int(head_top or 0)}\n")                     # canvas top -> top of head, px
        f.write(f"char_height={int(np.median(heights)) if heights else H}\n")  # idle character height, px
        f.write("head_fraction=0.45\n")                                 # clicks in the top 45% pat the head
        f.write("model=" + m.name + "\n")
    return W, H


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--map", nargs="*", default=None, help="state=animation pairs (default: automatic)")
    ap.add_argument("--fps", type=float, default=10)
    ap.add_argument("--scale", type=float, default=None, help="output pixels per skeleton unit (default: from --char-px)")
    ap.add_argument("--char-px", type=int, default=480, help="idle character height in output pixels")
    ap.add_argument("--ss", type=int, default=3, help="supersampling factor")
    ap.add_argument("--analyze", action="store_true", help="only print animation sizes")
    a = ap.parse_args()
    if a.analyze:
        m = Model(a.model_dir)
        for anim in m.sk.animations:
            b = m.visible_box([(anim, frame_times(anim, 4))])
            print(f"{m.name:18s} {anim.name:12s} {anim.duration:5.2f}s  visible {b[2]-b[0]:6.0f} x {b[3]-b[1]:6.0f}" if b else f"{anim.name} empty")
        return
    mapping = dict(kv.split("=", 1) for kv in a.map) if a.map else None
    export(a.model_dir, a.out_dir, a.fps, a.scale, a.ss, mapping, a.char_px)


if __name__ == "__main__":
    main()
