#!/usr/bin/env python3
"""Pre-render a Spine 3.8 SD (chibi) model or a dynamic painting into PNG frame sequences for BelfastPet.

  python3 tools/spine/render38.py MODEL_DIR OUT_DIR [--fps 12] [--scale 1.0] [--map state=anim ...]
  python3 tools/spine/render38.py --painting SKELETON OUT_DIR [--layer TOP_SKELETON] [--picture-px 720]

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
from skel38 import Animation, find_skeleton_files, load_skeleton, read_atlas  # noqa: E402
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
    """Content pixel (cx right, cy down, relative to the stripped image) -> page pixel.
    region.width/height are the unrotated content size; on the page a 90/270 degree region
    occupies height x width."""
    deg = getattr(region, "degrees", 90 if region.rotate else 0)
    w, h = region.width, region.height
    if deg == 90:    # packed turned a quarter counter-clockwise
        return region.x + cy, region.y + (w - cx)
    if deg == 180:   # upside down
        return region.x + (w - cx), region.y + (h - cy)
    if deg == 270:   # a quarter clockwise
        return region.x + (h - cy), region.y + cx
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


def is_premultiplied(img_u8):
    """True if an RGBA page looks premultiplied: colour never exceeds alpha on soft edges."""
    a = img_u8[..., 3].astype(np.int16)
    soft = (a > 0) & (a < 255)
    if soft.sum() < 1000:
        return False
    over = (img_u8[..., :3].max(axis=2).astype(np.int16) > a + 2) & soft
    return over.sum() < 0.02 * soft.sum()


def resolve_skeleton(path):
    """MODEL_DIR (holding <dir name>.skel, or exactly one skeleton) or a skeleton file
    (binary or JSON, any name) -> (skeleton path, atlas path, model name)."""
    if os.path.isdir(path):
        name = os.path.basename(os.path.normpath(path))
        skel = os.path.join(path, name + ".skel")
        if not os.path.exists(skel):
            found = find_skeleton_files(path)
            if len(found) != 1:
                raise SystemExit(f"{path}: expected one skeleton, found {[os.path.basename(f) for f in found]}")
            skel = found[0]
        folder = path
    else:
        skel, folder = path, os.path.dirname(os.path.abspath(path))
        name = os.path.splitext(os.path.basename(path))[0]
    stem = os.path.splitext(os.path.basename(skel))[0]
    atlas = os.path.join(folder, stem + ".atlas")
    if not os.path.exists(atlas):
        atlases = [f for f in os.listdir(folder) if f.endswith(".atlas")]
        if len(atlases) != 1:
            raise SystemExit(f"{skel}: no {stem}.atlas next to it")
        atlas = os.path.join(folder, atlases[0])
    return skel, atlas, name


class Model:
    hidden_slots = frozenset()  # slot names never drawn (e.g. a painting's scenery)

    def hide_slots(self, names):
        self.hidden_slots = frozenset(names)

    def __init__(self, model_dir, pma=False):
        """`model_dir`: a chibi folder (<name>/<name>.skel) or any Spine 3.8 skeleton file.
        `pma`: False = atlas pages hold straight alpha (the chibis), True = premultiplied,
        "auto" = decide per page from the pixels (the spinepaintings are premultiplied)."""
        skel, atlas, self.name = resolve_skeleton(model_dir)
        model_dir = os.path.dirname(os.path.abspath(atlas))
        self.sk = load_skeleton(skel)
        regions, pages = read_atlas(open(atlas, encoding="utf-8").read())
        prepare_attachments(self.sk, regions)
        self.tex = {}
        self.tex_scale = {}
        for p in pages:
            # Prefer a 2x super-resolved page (tools/spine/upscale.py); UVs are scaled to match.
            hi = os.path.join(model_dir, p[:-4] + "@2x.png")
            path, k = (hi, 2.0) if os.path.exists(hi) else (os.path.join(model_dir, p), 1.0)
            if pma is False:
                img = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32) / 255.0
                img[..., :3] *= img[..., 3:4]  # straight -> premultiplied alpha
            else:
                u8 = np.asarray(Image.open(path).convert("RGBA"))
                img = u8.astype(np.float32) / 255.0
                if not (pma is True or is_premultiplied(u8)):
                    img[..., :3] *= img[..., 3:4]
                else:  # keep colour <= alpha so blending stays well-defined
                    np.minimum(img[..., :3], img[..., 3:4], out=img[..., :3])
                del u8
            self.tex[p] = np.ascontiguousarray(img)
            self.tex_scale[p] = k
        self.pose = Pose(self.sk)
        self.so = load_raster()

    def anim(self, name):
        return self.sk.animation(name)

    def anim_names(self):
        return [a.name for a in self.sk.animations]

    def geometry(self, anim, t, overlays=None):
        """Draw list: ("draw", att, verts, colour, additive) / ("clip", polygon) / ("unclip",).
        `overlays`: [(anim, time), ...] layered on top of `anim` (see Pose.apply)."""
        pose = self.pose
        pose.apply(anim, t, overlays)
        out = []
        clip_end = None
        hidden = self.hidden_slots
        for si in pose.draw_order:
            slot = pose.slots[si]
            att = slot.attachment
            if hidden and slot.data.name in hidden:
                att = None
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
        """anims_times: [(anim, times) or (anim, times, overlays), ...]"""
        x0 = y0 = math.inf
        x1 = y1 = -math.inf
        for anim, times, *ov in anims_times:
            for t in times:
                for item in self.geometry(anim, t, ov[0] if ov else None):
                    if item[0] == "draw":
                        for x, y in item[2]:
                            x0, x1, y0, y1 = min(x0, x), max(x1, x), min(y0, y), max(y1, y)
        return x0, y0, x1, y1

    def render(self, anim, t, box, scale, ss, overlays=None):
        """One frame covering skeleton-space `box` -> straight-alpha RGBA uint8 array (y down)."""
        return self.rasterize(self.geometry(anim, t, overlays), box, scale, ss)

    def rasterize(self, items, box, scale, ss):
        """Draw list from geometry() -> straight-alpha RGBA uint8 array (y down)."""
        fp = ctypes.POINTER(ctypes.c_float)
        ip = ctypes.POINTER(ctypes.c_int)
        minx, miny, maxx, maxy = box
        W = max(1, int(math.ceil((maxx - minx) * scale)))
        H = max(1, int(math.ceil((maxy - miny) * scale)))
        base = np.zeros((H * ss, W * ss, 4), dtype=np.float32)
        fb, mask = base, None
        k = scale * ss
        for item in items:
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
        for anim, times, *ov in anims_times:
            for t in times[::step]:
                img = self.render(anim, t, gbox, coarse, 1, ov[0] if ov else None)
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


    def texel_density(self, anim, t=0.0):
        """Texture pixels per skeleton unit (area-weighted over what is drawn at `t`)."""
        tex_area = world_area = 0.0
        for item in self.geometry(anim, t):
            if item[0] != "draw":
                continue
            _, att, verts, _, _ = item
            k = self.tex_scale[att.page]
            uv = att.page_uv
            tris = att.tris
            for j in range(0, len(tris) - 2, 3):
                a, b, c = tris[j], tris[j + 1], tris[j + 2]
                world_area += abs((verts[b][0] - verts[a][0]) * (verts[c][1] - verts[a][1])
                                  - (verts[c][0] - verts[a][0]) * (verts[b][1] - verts[a][1]))
                tex_area += abs((uv[b][0] - uv[a][0]) * (uv[c][1] - uv[a][1])
                                - (uv[c][0] - uv[a][0]) * (uv[b][1] - uv[a][1])) * k * k
        return math.sqrt(tex_area / world_area) if world_area > 0 else 1.0

    def downsample_textures(self, factor):
        """Halve the atlas pages log2(factor) times (a mip level, premultiplied box filter) so
        heavily minified renders sample about one texel per supersample; saves RAM too."""
        while factor >= 2:
            for p, img in self.tex.items():
                h, w = img.shape[:2]
                img = np.pad(img, ((0, h % 2), (0, w % 2), (0, 0)), mode="edge")
                img = img.reshape(img.shape[0] // 2, 2, img.shape[1] // 2, 2, 4).mean(axis=(1, 3), dtype=np.float32)
                self.tex[p] = np.ascontiguousarray(img)
                self.tex_scale[p] *= 0.5
            factor //= 2


EMPTY_ANIM = Animation("", [], 0.0)


class LayeredAnim:
    """One animation name across the parts of a LayeredModel (EMPTY_ANIM where a part lacks it)."""

    def __init__(self, name, parts):
        self.name = name
        self.parts = parts
        self.duration = max(a.duration for a in parts)


class LayeredModel(Model):
    """Several skeletons drawn back to front around one origin, like the game stacks the
    SkeletonGraphics of a painting split into parts (e.g. xinnongB behind xinnongT)."""

    def __init__(self, paths, pma="auto", name=None):
        self.parts = [Model(p, pma) for p in paths]
        stems = [m.name for m in self.parts]
        self.name = name or (os.path.commonprefix(stems).rstrip("_-") or stems[0])
        self.tex, self.tex_scale = {}, {}
        for i, m in enumerate(self.parts):
            for page in m.tex:
                self.tex[f"{i}/{page}"] = m.tex[page]
                self.tex_scale[f"{i}/{page}"] = m.tex_scale[page]
            m.tex, m.tex_scale = {}, {}
            for _, table in m.sk.skins:
                for atts in table.values():
                    for a in atts.values():
                        if getattr(a, "page", None) is not None:
                            a.page = f"{i}/{a.page}"
        self.so = self.parts[0].so

    def anim(self, name):
        found = [m.anim(name) for m in self.parts]
        if all(a is None for a in found):
            return None
        return LayeredAnim(name, [a or EMPTY_ANIM for a in found])

    def anim_names(self):
        names = []
        for m in self.parts:
            names += [n for n in m.anim_names() if n not in names]
        return names

    def hide_slots(self, names):
        for m in self.parts:
            m.hide_slots(names)

    def geometry(self, anim, t, overlays=None):
        out = []
        for i, m in enumerate(self.parts):
            out += m.geometry(anim.parts[i], t, [(o.parts[i], ot) for o, ot in overlays or ()])
        return out


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


# ----------------------------------------------------------------------------- paintings

REACT_SEC = 1.6  # length of the expression reactions cut from the start of the idle loop


def quantize(folder):
    """256-colour palette PNGs via pngquant (same settings as tools/build_ships.py)."""
    import shutil
    exe = shutil.which("pngquant")
    if not exe:
        return "pngquant not found; frames left as full-colour PNG"
    files = [os.path.join(dp, f) for dp, _, fs in os.walk(folder) for f in fs if f.endswith(".png")]
    for i in range(0, len(files), 200):
        subprocess.run([exe, "--quality=80-98", "--speed=1", "--strip", "--skip-if-larger", "--force", "--ext", ".png",
                        *files[i:i + 200]], check=False)
    return None


def quantize_files(files, quality):
    import shutil
    exe = shutil.which("pngquant")
    for i in range(0, len(files), 200):
        subprocess.run([exe, f"--quality={quality}", "--speed=1", "--strip", "--skip-if-larger", "--force", "--ext", ".png",
                        *files[i:i + 200]], check=False)


def open_painting(skel_path, pma="auto"):
    """A skeleton file / folder, or a list of them (drawn back to front) -> Model."""
    if isinstance(skel_path, (list, tuple)):
        return LayeredModel(skel_path, pma) if len(skel_path) > 1 else Model(skel_path[0], pma)
    if os.path.isdir(skel_path):
        found = find_skeleton_files(skel_path)
        if len(found) > 1:  # parts of one painting, drawn in name order (xinnongB behind xinnongT, as in its prefab)
            return LayeredModel(found, pma, name=os.path.basename(os.path.normpath(skel_path)).removesuffix("_res"))
    return Model(skel_path, pma)


def idle_animation(m):
    anim = m.anim("normal")
    if anim is None:  # the longest animation is the ambient loop
        anims = [m.anim(n) for n in m.anim_names()]
        anim = max(anims, key=lambda a: a.duration)
    return anim


def scene_frame(m, base, times, vb, fill=0.97):
    """Paintings drawn on an opaque background rectangle (a full scene) are framed by it: in
    the game it fills the screen and whatever drifts past its edges (steam, petals) is
    off-screen. Returns that frame in skeleton units, or None for a cut-out picture."""
    k = 800.0 / max(vb[2] - vb[0], vb[3] - vb[1])
    opaque = None
    for t in times[::max(1, len(times) // 8)]:
        a = m.render(base, t, vb, k, 1)[..., 3] >= 250
        opaque = a if opaque is None else opaque | a
    ys, xs = np.nonzero(opaque)
    if len(xs) == 0:
        return None
    x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
    if opaque[y0:y1, x0:x1].mean() < fill:
        return None
    return vb[0] + x0 / k, vb[3] - y1 / k, vb[0] + x1 / k, vb[3] - y0 / k


def expression_names(m, base):
    """Zero-length poses ('1'..'9' in Azur Lane paintings) meant to be layered on the loop."""
    names = [n for n in m.anim_names() if n != base.name and m.anim(n).duration <= 0.05]
    return sorted(names, key=lambda n: (not n.isdigit(), int(n) if n.isdigit() else 0, n))


def changed_box(m, base, overlays, t=0.0):
    """Skeleton-space box around every attachment that `overlays` add, remove, move or
    recolour on top of `base` at time `t` (None when nothing changes)."""
    def draws(ov):
        return {id(item[1]): (item[2], item[3]) for item in m.geometry(base, t, ov) if item[0] == "draw"}

    a, b = draws(None), draws(overlays)
    pts = []
    for key in set(a) | set(b):
        if a.get(key) != b.get(key):
            for side in (a, b):
                if key in side and side[key][1][3] > 0.01:
                    pts += side[key][0]
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return min(xs), min(ys), max(xs), max(ys)


def pick_expressions(m, base, log=print):
    """Two expressions that change the face the most (and differ from each other), judged
    on renders of the region the expressions touch. Returns ([body, head], {name: score})
    or ([], {}) when the painting has none."""
    names = expression_names(m, base)
    boxes = {n: changed_box(m, base, [(m.anim(n), 0.0)]) for n in names}
    names = [n for n in names if boxes[n]]
    if not names:
        return [], {}
    x0 = min(boxes[n][0] for n in names)
    y0 = min(boxes[n][1] for n in names)
    x1 = max(boxes[n][2] for n in names)
    y1 = max(boxes[n][3] for n in names)
    margin = 0.1 * max(x1 - x0, y1 - y0)
    face = (x0 - margin, y0 - margin, x1 + margin, y1 + margin)
    scale = 400.0 / max(face[2] - face[0], face[3] - face[1])

    def img(overlay):
        ov = [(m.anim(overlay), 0.0)] if overlay else None
        return m.render(base, 0.0, face, scale, 2, ov).astype(np.int16)

    def changed(a, b):
        return int((np.abs(a - b).max(axis=2) > 32).sum())

    ref = img(None)
    renders = {n: img(n) for n in names}
    score = {n: changed(renders[n], ref) for n in names}
    log("  expressions, changed px of a 400 px face crop: " + ", ".join(f"{n}={score[n]}" for n in names))
    ranked = sorted((n for n in names if score[n] > 0), key=lambda n: -score[n])
    if not ranked:
        return [], score
    body = ranked[0]
    rest = [n for n in ranked[1:] if changed(renders[n], renders[body]) > 0]
    head = max(rest, key=lambda n: min(score[n], changed(renders[n], renders[body]))) if rest else body
    return [body, head], score


def ground_patch_alpha(cut_alpha, full_alpha, gp):
    """Alpha for a painting whose character sits on its scenery: the cut-out character,
    plus the scenery right around its lower part (the rock she sits on...), faded out.
    gp: {"top", "bottom"}: rows (fractions of the canvas) where the patch fades in;
    optional {"left", "right"}: columns where it fades in from the left; "grow"/"feather": px."""
    from PIL import ImageFilter
    H, W = cut_alpha.shape
    grow, feather = gp.get("grow", 40), gp.get("feather", 14)
    q = 4  # dilate on a quarter-size mask: cheap and smooth enough
    a = Image.fromarray(((cut_alpha > 12) * 255).astype(np.uint8)).resize((max(1, W // q), max(1, H // q)))
    a = a.filter(ImageFilter.MaxFilter(2 * max(1, grow // (2 * q)) + 1)).resize((W, H), Image.BILINEAR)
    a = a.filter(ImageFilter.GaussianBlur(feather))
    m = np.clip((np.asarray(a, dtype=np.float32) / 255 - 0.2) / 0.8, 0, 1)

    def ramp(n, lo, hi):
        v = np.clip((np.arange(n, dtype=np.float32) / n - lo) / max(1e-6, hi - lo), 0, 1)
        return v * v * (3 - 2 * v)

    m *= ramp(H, gp["top"], gp["bottom"])[:, None]
    if "left" in gp:
        m *= ramp(W, gp["left"], gp.get("right", gp["left"] + 0.1))[None, :]
    return np.maximum(cut_alpha.astype(np.float32), full_alpha.astype(np.float32) * m).astype(np.uint8)


def export_painting(skel_path, out_dir, fps=8.0, picture_px=720, expressions=None, ss=3, log=print, hide_slots=(),
                    ground_patch=None):
    """Pre-render a dynamic painting (Azur Lane spinepainting) for BelfastPet.

    `skel_path`: skeleton file (binary or JSON, any name), its folder, or a list of
    skeletons drawn back to front at one origin. The canvas is what is visible over the
    'normal' loop (a painting on an opaque background rectangle is cut to that rectangle),
    scaled so that it is `picture_px` tall. Writes idle/ (the whole 'normal' loop),
    react/ and react_head/ (the first REACT_SEC of the loop with an expression overlaid;
    `expressions`=(body, head) picks them, otherwise the two that change the picture most)
    and meta.ini. The painting stands still, so there are no walk/drag/fall/sleep/land
    folders: the app plays idle for those states. Returns (W, H)."""
    m = open_painting(skel_path)
    if hide_slots:
        m.hide_slots(hide_slots)
    base = idle_animation(m)
    idle_times = frame_times(base, fps)
    react_times = [i / fps for i in range(max(1, int(round(REACT_SEC * fps))))]

    # canvas: what is visible over the loop; scale: visible height -> picture_px
    vb = m.visible_box([(base, idle_times)], step=1 if len(idle_times) < 40 else 2)
    if vb is None:
        raise SystemExit(f"{m.name}: nothing visible in '{base.name}'")
    scene = scene_frame(m, base, idle_times, vb)
    if scene:
        log(f"  opaque background: canvas cut to its frame ({scene[2] - scene[0]:.0f} x {scene[3] - scene[1]:.0f} "
            f"of {vb[2] - vb[0]:.0f} x {vb[3] - vb[1]:.0f} visible units)")
        vb = scene
    scale = picture_px / (vb[3] - vb[1])

    if expressions:
        chosen = [e for e in expressions if m.anim(e)]
        chosen = (chosen * 2)[:2] if chosen else []
    else:
        chosen, _ = pick_expressions(m, base, log)
    overlays = {state: [(m.anim(e), 0.0)] for state, e in zip(("react", "react_head"), chosen)}
    if overlays and not scene:
        rb = m.visible_box([(base, react_times, ov) for ov in overlays.values()], step=2)
        if rb:
            vb = (min(vb[0], rb[0]), min(vb[1], rb[1]), max(vb[2], rb[2]), max(vb[3], rb[3]))

    # one pixel of visible_box's coarse render, plus a little room for antialiased edges
    gb = m.geometry_box([(base, idle_times)])
    pad = 0.0 if scene else 1.0 / min(0.25, 400.0 / max(gb[2] - gb[0], gb[3] - gb[1], 1)) + 2.0 / scale
    box = (vb[0] - pad, vb[1] - pad, vb[2] + pad, vb[3] + pad)
    W = int(math.ceil((box[2] - box[0]) * scale))
    H = int(math.ceil((box[3] - box[1]) * scale))

    # sample the atlas close to one texel per supersample (a mip level for big pictures)
    spacing = m.texel_density(base) / (scale * ss)
    mip = 2 ** int(round(math.log2(spacing))) if spacing >= 1.5 else 1
    if mip > 1:
        m.downsample_textures(mip)
    log(f"{m.name}: canvas {W}x{H} at {scale:.4f} px/unit, idle={base.name} ({len(idle_times)} frames), "
        f"expressions {chosen or 'none'}, texel spacing {spacing:.2f} -> mip 1/{mip}")

    plan = [("idle", idle_times, None)] + [(st, react_times, ov) for st, ov in overlays.items()]
    for state in ("idle", "react", "react_head"):
        d = os.path.join(out_dir, state)
        if os.path.isdir(d):
            for f in os.listdir(d):
                if f.endswith(".png"):
                    os.remove(os.path.join(d, f))
    head_top = None
    for state, times, ov in plan:
        d = os.path.join(out_dir, state)
        os.makedirs(d, exist_ok=True)
        for i, t in enumerate(times):
            img = m.render(base, t, box, scale, ss, ov)
            if ground_patch:
                hidden = m.hidden_slots if not hasattr(m, "parts") else m.parts[0].hidden_slots
                m.hide_slots(())
                full = m.render(base, t, box, scale, ss, ov)
                m.hide_slots(hidden)
                alpha = ground_patch_alpha(img[..., 3], full[..., 3], ground_patch)
                img = np.concatenate([np.where(img[..., 3:4] > 0, img[..., :3], full[..., :3]), alpha[..., None]], axis=2)
                # where the character is partly transparent over scenery, use the composed colour
                img[..., :3] = np.where((img[..., 3:4] > 0) & (full[..., 3:4] > img[..., 3:4]), full[..., :3], img[..., :3])
            if state == "idle":
                ys = np.nonzero(img[..., 3].max(axis=1) > 16)[0]
                if len(ys):
                    head_top = ys.min() if head_top is None else min(head_top, ys.min())
            Image.fromarray(img, "RGBA").save(os.path.join(d, f"{i:03d}.png"))
    for state in ("react", "react_head"):  # an unused folder from an earlier export would be played
        d = os.path.join(out_dir, state)
        if state not in overlays and os.path.isdir(d) and not os.listdir(d):
            os.rmdir(d)
    with open(os.path.join(out_dir, "meta.ini"), "w") as f:
        f.write("; written by tools/spine/render38.py (export_painting)\n[sequence]\n")
        f.write(f"fps={fps:g}\nwidth={W}\nheight={H}\n")
        f.write("ground=0\n")                              # the picture stands on the canvas bottom
        f.write(f"head_top={int(head_top or 0)}\n")        # canvas top -> top of the visible picture, px
        f.write(f"char_height={H}\n")
        f.write("head_fraction=0.25\n")                    # clicks in the top quarter pat the head
        f.write("kind=painting\nwalk=0\n")
        f.write("model=" + m.name + "\n")
    note = quantize(out_dir)
    if note:
        log(note)
    else:
        # Full scenes (a background behind the character) miss pngquant's quality floor of
        # 80 by a little (Q ~76) and would stay 32-bit: 3-4x the disk space and 4x the RAM
        # in the app, which keeps <= 256-colour frames as one byte per pixel.
        left = []
        for dp, _, fs in os.walk(out_dir):
            for f in fs:
                if f.endswith(".png"):
                    with Image.open(os.path.join(dp, f)) as im:
                        if im.mode != "P":
                            left.append(os.path.join(dp, f))
        if left:
            log(f"  {len(left)} frames missed pngquant's quality floor; quantizing them at 50-98")
            quantize_files(left, "50-98")
    return W, H


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("model_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--map", nargs="*", default=None, help="state=animation pairs (default: automatic)")
    ap.add_argument("--fps", type=float, default=None, help="frames per second (default 10, paintings 8)")
    ap.add_argument("--scale", type=float, default=None, help="output pixels per skeleton unit (default: from --char-px)")
    ap.add_argument("--char-px", type=int, default=480, help="idle character height in output pixels")
    ap.add_argument("--ss", type=int, default=3, help="supersampling factor")
    ap.add_argument("--analyze", action="store_true", help="only print animation sizes")
    ap.add_argument("--painting", action="store_true",
                    help="model_dir is a dynamic painting skeleton (file or folder): export_painting")
    ap.add_argument("--layer", action="append", default=[], help="painting: another skeleton drawn on top (repeatable)")
    ap.add_argument("--picture-px", type=int, default=720, help="painting: visible picture height in output pixels")
    ap.add_argument("--expressions", nargs=2, default=None, metavar=("BODY", "HEAD"),
                    help="painting: expression animations for react/ and react_head/")
    a = ap.parse_args()
    if a.painting:
        src = [a.model_dir] + a.layer if a.layer else a.model_dir
        export_painting(src, a.out_dir, a.fps or 8.0, a.picture_px, a.expressions, a.ss)
        return
    a.fps = a.fps or 10.0
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
