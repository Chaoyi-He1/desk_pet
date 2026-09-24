#!/usr/bin/env python3
"""Regression and unit checks for tools/spine.

  python3 tools/spine/test_spine.py            run all checks
  python3 tools/spine/test_spine.py --record   print fresh chibi hashes (paste into CHIBI_BASELINE)

1. Chibi models render bit-identically to the recorded baseline (geometry floats and pixels).
2. The JSON skeleton reader: every field and convention on a small synthetic skeleton, and
   the expected structure for beierfasite_g.
3. Path constraints: Spine 3.8 semantics on synthetic paths, and the bone the constraint in
   beierfasite_9 drives travels its path over the 'normal' loop.
4. Expression overlays change the pose on top of 'normal' without resetting it.
Checks whose source files are missing are skipped, not failed.
"""
import hashlib
import math
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402

import render38  # noqa: E402
import skel38  # noqa: E402
from pose38 import Pose  # noqa: E402

UNPACKED = os.path.join(ROOT, "assets", "official", "game", "unpacked")
CHAR = os.path.join(UNPACKED, "char")
PAINT = os.path.join(UNPACKED, "spinepainting")

CHIBI_MODELS = ["beierfasite", "chaijun_5", "xinnong_5"]
CHIBI_ANIMS = ["normal", "stand2", "walk", "motou", "touch", "tuozhuai"]
CHIBI_TIMES = [0.0, 0.37, 1.1]

# Recorded from the renderer before JSON / path-constraint / overlay support was added
# (commit fa49cb3). (model, anim, time) -> "geometry sha1:pixels sha1:WxH".
CHIBI_BASELINE = {
    "beierfasite/normal/0": "dafbc483ce0db778:49fe9028e4bc2433:279x313",
    "beierfasite/normal/0.37": "2af751c751c692d3:651466d528da0b46:282x314",
    "beierfasite/normal/1.1": "0289682bd4983e43:3b5267b4d4204980:288x314",
    "beierfasite/stand2/0": "44f678febfa7c8c9:a55734a334b2c694:201x313",
    "beierfasite/stand2/0.37": "1031387874ed33d4:0e7003df08bc9340:202x313",
    "beierfasite/stand2/1.1": "5060a9af8dd1879e:14c82f56402281a7:215x315",
    "beierfasite/walk/0": "8f6933e20e36928b:60f70438e2b89a82:203x319",
    "beierfasite/walk/0.37": "2f7cd544e2e0f8b1:a7ea6d2bd3e8371d:213x321",
    "beierfasite/walk/1.1": "8f6933e20e36928b:60f70438e2b89a82:203x319",
    "beierfasite/motou/0": "aef6ea8349098e77:10dc56e668a107db:200x313",
    "beierfasite/motou/0.37": "196b9a9f1fc4c47d:56c6c67764068f25:229x325",
    "beierfasite/motou/1.1": "565399e1c24b2b86:8a2bcd61ca97df58:240x327",
    "beierfasite/touch/0": "aef6ea8349098e77:10dc56e668a107db:200x313",
    "beierfasite/touch/0.37": "23ba8bb422bc41fb:4d9083edf5d0f5ea:215x305",
    "beierfasite/touch/1.1": "aef6ea8349098e77:10dc56e668a107db:200x313",
    "beierfasite/tuozhuai/0": "d45748c657f609c9:65486faf61346563:289x372",
    "beierfasite/tuozhuai/0.37": "db964c30fec74e5b:9a3bd3693a4240db:310x366",
    "beierfasite/tuozhuai/1.1": "d45748c657f609c9:65486faf61346563:289x372",
    "chaijun_5/normal/0": "b86a6c97148f4b31:3bf1a47c9d03d48c:271x402",
    "chaijun_5/normal/0.37": "9b1dca0d1e534241:43f761382ecefdab:268x404",
    "chaijun_5/normal/1.1": "4fddf7a89689e3b8:fc0b988cbdbeb4d8:283x407",
    "chaijun_5/stand2/0": "034f02ce1815b1ad:780f3bde741baa25:219x402",
    "chaijun_5/stand2/0.37": "384d4cac554712a3:12d5e0f5d9c3f9fd:214x403",
    "chaijun_5/stand2/1.1": "1c85210141abd044:824de17afc82e584:216x404",
    "chaijun_5/walk/0": "6b8a726071e2b3ef:9b4e814b342ebcfd:217x404",
    "chaijun_5/walk/0.37": "b0a412fbff79ed31:6d337a80d844eb5c:231x411",
    "chaijun_5/walk/1.1": "b1c1b91299ab4384:9b4e814b342ebcfd:217x404",
    "chaijun_5/motou/0": "8adc85e41f13f942:2bc40cc30983953c:215x402",
    "chaijun_5/motou/0.37": "348279f7143eeb6e:ebbca3b4b0047c5b:293x442",
    "chaijun_5/motou/1.1": "1480971658bcfd65:a4f36264e50a0bd6:271x344",
    "chaijun_5/touch/0": "82af7491077c677c:a9b1b7d629b340e7:215x402",
    "chaijun_5/touch/0.37": "093a44b3aa810157:7e5fb39901dd1f56:247x490",
    "chaijun_5/touch/1.1": "82af7491077c677c:a9b1b7d629b340e7:215x402",
    "chaijun_5/tuozhuai/0": "b69127176ada5e30:266966da73296633:423x431",
    "chaijun_5/tuozhuai/0.37": "8da9ccb980645cc9:5b3e549c6924e2c7:426x434",
    "chaijun_5/tuozhuai/1.1": "3b223a251b9a476e:f5aa05deb7d9ea14:436x417",
    "xinnong_5/normal/0": "18646fa5619f5181:dc4002c742fd9270:468x416",
    "xinnong_5/normal/0.37": "ac65ad6513913eae:cceb85235d32ba53:469x412",
    "xinnong_5/normal/1.1": "d1e4ee3e622498e4:85ad88d98df2c939:472x416",
    "xinnong_5/stand2/0": "7c02bd51b436dc55:4696f9abfb978741:292x410",
    "xinnong_5/stand2/0.37": "2635b6ba6291822c:143ed8a3132d0860:284x412",
    "xinnong_5/stand2/1.1": "aa55f8f23fe3e11f:c627cdcfe1d8bc73:314x415",
    "xinnong_5/walk/0": "52200bad300133e3:d101d1dec7f7cf04:290x415",
    "xinnong_5/walk/0.37": "a1a40805bd18e7f0:4234e81eb61cf7e5:287x412",
    "xinnong_5/walk/1.1": "8abbe978906a7a71:7c154d33f7c4424c:303x417",
    "xinnong_5/motou/0": "2e852bc1205fdaed:8cc95bd96d0f8ec4:292x410",
    "xinnong_5/motou/0.37": "7863dcae3a2d6986:3fb8589f4ecb65b5:1002x814",
    "xinnong_5/motou/1.1": "65fb992586300181:697bb3d5a36a2180:746x803",
    "xinnong_5/touch/0": "6cd39320a70ed8a7:4696f9abfb978741:292x410",
    "xinnong_5/touch/0.37": "72e14d096f8fea28:6f7588b30d03d1a4:337x368",
    "xinnong_5/touch/1.1": "d45bb577a7b79050:8e2ccca6f88d7d37:292x410",
    "xinnong_5/tuozhuai/0": "307b5455a73dfa68:80f0ee2659bd3cb6:509x408",
    "xinnong_5/tuozhuai/0.37": "60ad718f61995281:d0b4f54dfca0e07b:541x401",
    "xinnong_5/tuozhuai/1.1": "0534e80a3b25cdcc:4ef0642545b1d8f0:516x416",
}


def chibi_hashes(models=CHIBI_MODELS):
    out = {}
    for name in models:
        d = os.path.join(CHAR, name)
        if not os.path.isdir(d):
            continue
        m = render38.Model(d)
        for an in CHIBI_ANIMS:
            anim = m.anim(an)
            if anim is None:
                continue
            for t in CHIBI_TIMES:
                g = hashlib.sha1()
                for item in m.geometry(anim, t):
                    if item[0] == "draw":
                        g.update(item[1].name.encode())
                        g.update(struct.pack("<%dd" % (2 * len(item[2])), *[c for v in item[2] for c in v]))
                        g.update(struct.pack("<4d?", *item[3], item[4]))
                    elif item[0] == "clip":
                        g.update(struct.pack("<%dd" % (2 * len(item[1])), *[c for v in item[1] for c in v]))
                    else:
                        g.update(b"unclip")
                box = m.geometry_box([(anim, [t])])
                img = m.render(anim, t, box, 1.0, 2)
                out[f"{name}/{an}/{t:g}"] = "%s:%s:%dx%d" % (g.hexdigest()[:16], hashlib.sha1(img.tobytes()).hexdigest()[:16],
                                                            img.shape[1], img.shape[0])
    return out


def check_chibi():
    if not CHIBI_BASELINE:
        return "skip (no baseline recorded)"
    got = chibi_hashes()
    if not got:
        return "skip (chibi sources missing)"
    bad = [k for k, v in got.items() if CHIBI_BASELINE.get(k) != v]
    missing = [k for k in CHIBI_BASELINE if k not in got]
    assert not bad, "chibi output changed: " + ", ".join(bad[:8])
    assert not missing, "chibi frames not rendered: " + ", ".join(missing[:8])
    return f"{len(got)} frames identical"


# ----------------------------------------------------------------------------- JSON reader

SYNTHETIC = {
    "skeleton": {"hash": "h", "spine": "3.8.99", "x": -1, "y": -2, "width": 30, "height": 40},
    "bones": [
        {"name": "root"},
        {"name": "a", "parent": "root", "length": 100, "rotation": 10, "x": 5, "y": 6, "scaleX": 2,
         "shearY": 3, "transform": "noScale"},
        {"name": "b", "parent": "a", "length": 50},
        {"name": "t", "parent": "root", "x": 50},
    ],
    "slots": [
        {"name": "s0", "bone": "root", "attachment": "reg", "color": "ff000080", "blend": "additive"},
        {"name": "s1", "bone": "a", "attachment": "mesh", "dark": "102030", "blend": "screen"},
        {"name": "path", "bone": "root", "attachment": "line"},
        {"name": "clip", "bone": "root", "attachment": "clipper"},
    ],
    "ik": [{"name": "ik1", "order": 2, "bones": ["a", "b"], "target": "t", "bendPositive": False, "mix": 0.5,
            "softness": 3, "stretch": True}],
    "transform": [{"name": "tc", "order": 1, "bones": ["b"], "target": "t", "rotation": 5, "x": 1, "y": 2,
                   "scaleX": 0.1, "shearY": 4, "rotateMix": 0.5, "translateMix": 0, "local": True}],
    "path": [{"name": "pc", "order": 0, "bones": ["a"], "target": "path", "positionMode": "fixed",
              "spacingMode": "percent", "rotateMode": "chainScale", "position": 7, "rotation": 15}],
    "skins": {"default": {  # the pre-3.8 dict form; the list form is covered by beierfasite_g
        "s0": {"reg": {"x": 1, "y": 2, "width": 10, "height": 20, "color": "00ff00ff"}},
        "s1": {"mesh": {"type": "mesh", "uvs": [0, 0, 1, 0, 1, 1], "triangles": [0, 1, 2],
                        "vertices": [0, 0, 10, 0, 10, 10], "hull": 3},
               "wmesh": {"type": "mesh", "uvs": [0, 0, 1, 0, 1, 1], "triangles": [0, 1, 2], "hull": 3,
                         "vertices": [1, 1, 0, 0, 1, 2, 1, 10, 0, 0.5, 2, 5, 5, 0.5, 1, 2, 10, 10, 1]},
               "lm": {"type": "linkedmesh", "parent": "mesh", "deform": False, "path": "other"},
               "pt": {"type": "point", "x": 3, "y": 4, "rotation": 90},
               "bb": {"type": "boundingbox", "vertexCount": 3, "vertices": [0, 0, 1, 0, 0, 1]}},
        "path": {"line": {"type": "path", "vertexCount": 6, "lengths": [300, 600], "constantSpeed": False,
                          "vertices": [-10, 0, 0, 0, 100, 0, 200, 0, 300, 0, 310, 0]}},
        "clip": {"clipper": {"type": "clipping", "end": "s1", "vertexCount": 3, "vertices": [0, 0, 100, 0, 0, 100]}},
    }},
    "events": {"ev": {"int": 1}},
    "animations": {"anim": {
        "bones": {"a": {"rotate": [{"angle": 10, "curve": "stepped"}, {"time": 1, "angle": 20, "curve": 0.25, "c3": 0.75},
                                   {"time": 2, "angle": 30}],
                        "translate": [{"x": 1}], "scale": [{"time": 0.5, "y": 2}]}},
        "slots": {"s0": {"attachment": [{"time": 0.5, "name": None}],
                         "color": [{"color": "ffffff00", "curve": [0.1, 0.2, 0.3, 0.4]}, {"time": 1, "color": "ffffffff"}]},
                  "s1": {"twoColor": [{"light": "ff0000ff", "dark": "00ff00"}]}},
        "ik": {"ik1": [{"mix": 0.25}]},
        "transform": {"tc": [{"time": 0.25, "rotateMix": 0.1}]},
        "path": {"pc": {"position": [{"position": 10}], "spacing": [{"time": 1, "spacing": 5}],
                        "mix": [{"rotateMix": 0.5}]}},
        "deform": {"default": {"s1": {"mesh": [{"offset": 2, "vertices": [1, 1]}, {"time": 1}],
                                      "wmesh": [{"offset": 1, "vertices": [7]}]}}},
        "drawOrder": [{"time": 0.5, "offsets": [{"slot": "clip", "offset": -3}]}, {"time": 1}],
        "events": [{"time": 0.75, "name": "ev"}],
    }},
}


def close(a, b, eps=1e-6):
    if isinstance(a, (list, tuple)):
        return len(a) == len(b) and all(close(x, y, eps) for x, y in zip(a, b))
    return abs(a - b) <= eps


def check_json_synthetic():
    sk = skel38.read_skel_json(SYNTHETIC)
    assert sk.version == "3.8.99" and (sk.x, sk.width) == (-1, 30)
    a = sk.bones[1]
    assert (a.parent, a.rotation, a.x, a.scale_x, a.scale_y, a.shear_y, a.length, a.transform_mode) == \
        (0, 10, 5, 2, 1, 3, 100, "noScale"), a
    s0, s1 = sk.slots[0], sk.slots[1]
    assert close(s0.color, (1, 0, 0, 128 / 255)) and s0.blend == 1 and s0.dark is None, s0
    assert s1.dark == 0x102030 and s1.blend == 3 and s1.bone == 1 and s1.color == (1, 1, 1, 1), s1
    ik = sk.iks[0]
    assert (ik.bones, ik.target, ik.bend, ik.mix, ik.softness, ik.stretch, ik.compress) == ([1, 2], 3, -1, 0.5, 3, True, False)
    tc = sk.transforms[0]
    assert tc.offsets == [5, 1, 2, 0.1, 0, 4] and tc.mixes == [0.5, 0, 1, 1] and tc.local and not tc.relative, tc
    pc = sk.paths[0]
    assert (pc.bones, pc.target, pc.position_mode, pc.spacing_mode, pc.rotate_mode, pc.position, pc.rotation,
            pc.rotate_mix) == ([1], 2, "fixed", "percent", "chainScale", 7, 15, 1), pc
    assert [n for n, _ in sk.skins] == ["default"]
    t = sk.skins[0][1]
    reg = t[0]["reg"]
    assert (reg.type, reg.path, reg.x, reg.y, reg.width, reg.height, reg.scale_x, reg.color) == \
        ("region", "reg", 1, 2, 10, 20, 1, (0, 1, 0, 1)), reg
    mesh, wmesh, lm = t[1]["mesh"], t[1]["wmesh"], t[1]["lm"]
    assert mesh.bones is None and mesh.vertices == [0, 0, 10, 0, 10, 10] and mesh.vertex_count == 3
    assert wmesh.bones == [1, 1, 2, 1, 2, 1, 2] and wmesh.vertices == [0, 0, 1, 10, 0, 0.5, 5, 5, 0.5, 10, 10, 1], wmesh
    assert lm.type == "mesh" and lm.path == "other" and lm.vertices is mesh.vertices and lm.deform_parent is None
    assert t[1]["pt"].type == "point" and t[1]["bb"].vertex_count == 3
    line = t[2]["line"]
    assert (line.type, line.closed, line.constant_speed, line.vertex_count, line.lengths) == ("path", False, False, 6, [300, 600])
    assert t[3]["clipper"].end_slot == 1 and len(t[3]["clipper"].vertices) == 6
    assert sk.events == [("ev", None)]
    an = sk.animation("anim")
    assert close(an.duration, 2.0)
    tl = {(x.kind, x.target if not isinstance(x.target, tuple) else x.target[2]): x for x in an.timelines}
    rot = tl[("rotate", 1)].frames
    assert rot[0] == (0.0, 10.0, "stepped") and rot[1] == (1.0, 20.0, (0.25, 0.0, 0.75, 1.0)) and rot[2][2] is None
    assert tl[("translate", 1)].frames == [(0.0, (1.0, 0.0), None)]
    assert tl[("scale", 1)].frames == [(0.5, (1.0, 2.0), None)]
    assert tl[("attachment", 0)].frames == [(0.5, None, None)]
    col = tl[("color", 0)].frames
    assert col[0][2] == (0.1, 0.2, 0.3, 0.4) and close(col[0][1], (1, 1, 1, 0))
    assert tl[("twocolor", 1)].frames[0][1] == ((1, 0, 0, 1), 0x00ff00)
    assert tl[("ik", 0)].frames[0][1] == (0.25, 0.0, 1, False, False)
    assert tl[("transform", 0)].frames[0][1] == (0.1, 1.0, 1.0, 1.0)
    assert tl[("path_position", 0)].frames[0][1] == 10 and tl[("path_spacing", 0)].frames[0][:2] == (1.0, 5.0)
    assert tl[("path_mix", 0)].frames[0][1] == (0.5, 1.0)
    # deform: unweighted frames hold absolute vertices, weighted ones offsets; no "vertices" = setup / zero
    md = tl[("deform", "mesh")].frames
    assert md[0][1] == [0, 0, 11, 1, 10, 10] and md[1][1] == [0, 0, 10, 0, 10, 10], md
    assert tl[("deform", "wmesh")].frames[0][1] == [0, 7, 0, 0, 0, 0, 0, 0]
    dro = tl[("draworder", None)].frames
    assert dro[0][1] == [3, 0, 1, 2] and dro[1][1] == [0, 1, 2, 3], dro  # clip moved 3 places back
    assert tl[("event", None)].frames == [(0.75, "ev", None)]
    Pose(sk).apply(an, 0.6)  # everything the reader produced is usable
    return "all fields and conventions as expected"


def check_json_painting():
    path = os.path.join(PAINT, "beierfasite_g_res", "beierfasite_g")
    if not os.path.exists(path):
        return "skip (beierfasite_g missing)"
    sk = skel38.load_skeleton(path)
    assert sk.version == "3.8.99"
    assert (len(sk.bones), len(sk.slots), len(sk.iks), len(sk.transforms), len(sk.paths)) == (442, 150, 16, 43, 0)
    assert [n for n, _ in sk.skins] == ["default"]
    assert sorted(a.name for a in sk.animations) == [str(i) for i in range(1, 10)] + ["normal"]
    kinds = {}
    for _, table in sk.skins:
        for atts in table.values():
            for a in atts.values():
                kinds[a.type] = kinds.get(a.type, 0) + 1
    assert kinds == {"mesh": 161, "region": 9, "clipping": 2}, kinds
    assert sum(s.blend == 1 for s in sk.slots) == 8
    normal = sk.animation("normal")
    assert 16 < normal.duration < 18 and len(normal.timelines) == 491, (normal.duration, len(normal.timelines))
    for tl in normal.timelines:  # deform frames match their meshes
        if tl.kind == "deform":
            att = sk.skins[tl.target[0]][1][tl.target[1]][tl.target[2]]
            n = len(att.vertices) // 3 * 2 if att.bones else len(att.vertices)
            assert all(len(f[1]) == n for f in tl.frames)
    # the binary reader still reads the other paintings, path constraints included
    bin_path = os.path.join(PAINT, "beierfasite_9_res", "beierfasite_9.skel")
    if os.path.exists(bin_path):
        sk9 = skel38.load_skeleton(bin_path)
        assert len(sk9.paths) == 1 and sk9.paths[0].name == "pingai" and sk9.paths[0].rotate_mode == "tangent"
    return "442 bones, 150 slots, 1 skin, 10 animations"


# ----------------------------------------------------------------------------- path constraint

def path_skeleton(path_vertices, lengths, mode="chain", position_mode="fixed", spacing_mode="length",
                  position=0.0, spacing=0.0, constant_speed=True, mixes=(1, 1), closed=False):
    """Three 100-unit bones lying along +x, constrained to a path in slot 'p'."""
    return skel38.read_skel_json({
        "skeleton": {"spine": "3.8.99"},
        "bones": [{"name": "root"}, {"name": "pb", "parent": "root"},
                  {"name": "a", "parent": "root", "length": 100},
                  {"name": "b", "parent": "a", "length": 100, "x": 100},
                  {"name": "c", "parent": "b", "length": 100, "x": 100}],
        "slots": [{"name": "p", "bone": "pb", "attachment": "path"}],
        "path": [{"name": "pc", "bones": ["a", "b", "c"], "target": "p", "rotateMode": mode,
                  "positionMode": position_mode, "spacingMode": spacing_mode, "position": position,
                  "spacing": spacing, "rotateMix": mixes[0], "translateMix": mixes[1]}],
        "skins": [{"name": "default", "attachments": {"p": {"path": {
            "type": "path", "closed": closed, "constantSpeed": constant_speed,
            "vertexCount": len(path_vertices) // 2, "lengths": lengths, "vertices": path_vertices}}}}],
        "animations": {"idle": {}, "move": {"bones": {"pb": {"translate": [{"x": 40}]}}}},
    })


def bone_state(pose, name):
    b = next(b for b in pose.bones if b.data.name == name)
    return b.wx, b.wy, math.degrees(math.atan2(b.c, b.a)), math.hypot(b.a, b.c)


def posed(sk, anim="idle"):
    p = Pose(sk)
    p.apply(sk.animation(anim), 0.0)
    return p


def check_path_synthetic():
    up = [0, -10, 0, 0, 0, 100, 0, 200, 0, 300, 0, 310]           # straight line (0,0) -> (0,300)
    # chain, length spacing: joints 100 apart up the line, all turned to 90 degrees
    p = posed(path_skeleton(up, [300, 600]))
    for n, y in (("a", 0), ("b", 100), ("c", 200)):
        x, yy, r, _ = bone_state(p, n)
        assert close((x, yy, r), (0, y, 90), 1e-6), (n, x, yy, r)
    # tangent, percent position, fixed spacing
    p = posed(path_skeleton(up, [300, 600], "tangent", "percent", "fixed", 0.5, 50))
    assert [round(bone_state(p, n)[1], 6) for n in "abc"] == [150, 200, 250]
    # percent spacing
    p = posed(path_skeleton(up, [300, 600], "tangent", "percent", "percent", 0.5, 0.25))
    assert [round(bone_state(p, n)[1], 6) for n in "abc"] == [150, 225, 300]
    # past the end of an open path: continue along the end tangent
    p = posed(path_skeleton(up, [300, 600], "tangent", "fixed", "length", 250))
    assert [round(bone_state(p, n)[1], 6) for n in "abc"] == [250, 350, 450]
    # mixes: halfway between the setup pose and the path
    p = posed(path_skeleton(up, [300, 600], "tangent", "percent", "fixed", 0.5, 0, mixes=(0.5, 0.5)))
    x, y, r, _ = bone_state(p, "a")
    assert close((x, y, r), (0, 75, 45), 1e-6), (x, y, r)
    # the path's own bone moves first (update order): the chain follows it
    sk = path_skeleton(up, [300, 600])
    p = posed(sk, "move")
    assert close(bone_state(p, "a")[:2], (40, 0)) and close(bone_state(p, "c")[:2], (40, 200))
    # constant speed vs. setup lengths on an unevenly parameterised line (handles at the ends):
    # y(t) = 300 (3t^2 - 2t^3). Constant speed puts 75 units (25%) at the 10-chord estimate:
    # between y(0.3) = 64.8 and y(0.4) = 105.6 -> t = 0.325 -> y = 74.465625 (the true point is 75)
    uneven = [0, 0, 0, 0, 0, 0, 0, 300, 0, 300, 0, 300]
    p = posed(path_skeleton(uneven, [300, 300], "tangent", "percent", "fixed", 0.25, 0, constant_speed=True))
    assert close(bone_state(p, "a")[1], 74.465625, 1e-6), bone_state(p, "a")
    p = posed(path_skeleton(uneven, [300, 300], "tangent", "percent", "fixed", 0.25, 0, constant_speed=False))
    assert close(bone_state(p, "a")[1], 300 * (3 / 16 - 2 / 64), 1e-6), bone_state(p, "a")
    # a curve (quarter circle of radius 300): chainScale stretches each bone onto the next sample,
    # chain keeps the bones rigid and starts each one at the tip of the previous one
    k = 0.5523 * 300
    arc = [300, -k, 300, 0, 300, k, k, 300, 0, 300, -k, 300]
    ps = posed(path_skeleton(arc, [471.2, 942.5], "chainScale"))
    pc = posed(path_skeleton(arc, [471.2, 942.5], "chain"))
    for n, nxt in (("a", "b"), ("b", "c")):
        x, y, r, s = bone_state(ps, n)
        assert close(math.hypot(x, y), 300, 0.5), (n, x, y)             # on the arc
        x2, y2, _, _ = bone_state(ps, nxt)
        tip = (x + 100 * s * math.cos(math.radians(r)), y + 100 * s * math.sin(math.radians(r)))
        # the bone is scaled to reach the next sample: the chord between samples 100 estimated
        # arc units apart (the 4-chord length table runs a little short on a curve) is not 100
        assert close(tip, (x2, y2), 1e-6) and s != 1 and abs(s - 1) < 0.01, (n, tip, (x2, y2), s)
        x, y, r, s = bone_state(pc, n)
        x2, y2, _, _ = bone_state(pc, nxt)
        tip = (x + 100 * math.cos(math.radians(r)), y + 100 * math.sin(math.radians(r)))
        assert close(tip, (x2, y2), 1e-6) and close(s, 1), (n, tip, (x2, y2))
    return "positions, spacing modes, rotate modes, mixes, ends, update order"


def check_path_painting():
    path = os.path.join(PAINT, "beierfasite_9_res", "beierfasite_9.skel")
    if not os.path.exists(path):
        return "skip (beierfasite_9 missing)"
    sk = skel38.load_skeleton(path)
    pc = sk.paths[0]
    pose = Pose(sk)
    normal = sk.animation("normal")
    bone = pose.bones[pc.bones[0]]
    slot = pose.slots[pc.target]
    seen = []
    for t in (0.0, 9.7, 9.9, 10.1, 11.5):
        pose.apply(normal, t)
        pts = pose.world_vertices(slot)
        seen.append((bone.wx, bone.wy))
        if t == 0.0:    # position 0: the start of the path
            assert close((bone.wx, bone.wy), pts[1], 1e-6), ((bone.wx, bone.wy), pts[1])
        if t == 11.5:   # position 1: its end
            assert close((bone.wx, bone.wy), pts[-2], 1e-6), ((bone.wx, bone.wy), pts[-2])
    moves = [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(seen, seen[1:])]
    assert all(mv > 50 for mv in moves), moves
    return f"'{sk.bones[pc.bones[0]].name}' travels the path: " + ", ".join(f"{mv:.0f}" for mv in moves) + " units"


# ----------------------------------------------------------------------------- overlays

def check_overlay():
    path = os.path.join(PAINT, "beierfasite_g_res", "beierfasite_g")
    if not os.path.exists(path):
        return "skip (beierfasite_g missing)"
    sk = skel38.load_skeleton(path)
    pose = Pose(sk)
    normal, expr = sk.animation("normal"), sk.animation("5")
    pose.apply(normal, 3.0)
    plain = [(b.wx, b.wy, b.a, b.b, b.c, b.d) for b in pose.bones]
    plain_att = [s.attachment for s in pose.slots]
    pose.apply(normal, 3.0, [(expr, 0.0)])
    over = [(b.wx, b.wy, b.a, b.b, b.c, b.d) for b in pose.bones]
    for tl in expr.timelines:   # the expression's keys win
        if tl.kind == "attachment":
            att = pose.slots[tl.target].attachment
            assert (att.name if att else None) == tl.frames[0][1], sk.slots[tl.target].name
    keyed = {tl.target for tl in expr.timelines if tl.kind in ("rotate", "translate", "scale", "shear")}
    changed = {i for i, (p, o) in enumerate(zip(plain, over)) if p != o}
    same = len(plain) - len(changed)
    # the rest keeps the 'normal' pose (no reset to setup in between): only the keyed bones and
    # the bones that hang off them or follow them through constraints move
    assert same > 0.9 * len(plain), (same, len(plain))
    assert changed, "expression changed nothing"
    setup = Pose(sk)
    setup.apply(expr, 0.0)
    animated = [i for i, b in enumerate(setup.bones) if (b.wx, b.wy) != plain[i][:2] and i not in changed]
    assert animated, "no bone animated by 'normal' survived the overlay"
    assert sum(a is not b for a, b in zip(plain_att, [s.attachment for s in pose.slots])) >= 1
    return f"{len(changed)} of {len(plain)} bones changed ({len(keyed)} keyed), {len(animated)} animated bones kept"


def check_model_paths():
    """Model finds skeleton and atlas for chibi folders, oddly named JSON files and split paintings."""
    out = []
    d = os.path.join(CHAR, "beierfasite")
    if os.path.isdir(d):
        skel, atlas, name = render38.resolve_skeleton(d)
        assert (os.path.basename(skel), os.path.basename(atlas), name) == ("beierfasite.skel", "beierfasite.atlas", "beierfasite")
        out.append("chibi folder")
    f = os.path.join(PAINT, "beierfasite_g_res", "beierfasite_g")
    if os.path.exists(f):
        skel, atlas, name = render38.resolve_skeleton(f)
        assert (skel, os.path.basename(atlas), name) == (f, "beierfasite_g.atlas", "beierfasite_g")
        skel, _, _ = render38.resolve_skeleton(os.path.dirname(f))  # folder with one extension-less JSON
        assert skel == f, skel
        out.append("JSON file")
    d = os.path.join(PAINT, "xinnong_res")
    if os.path.isdir(d):
        assert [os.path.basename(p) for p in skel38.find_skeleton_files(d)] == ["xinnongB.skel", "xinnongT.skel"]
        out.append("split painting")
    return ", ".join(out) or "skip (sources missing)"


def check_atlas_rotation():
    """Regions packed at 0/90/180/270 degrees: a region's content corners land on the right
    page pixels, and real meshes on 180/270 regions sample their own opaque pixels."""
    from skel38 import read_atlas
    text = "p.png\nsize: 64,64\nformat: RGBA8888\nfilter: Linear,Linear\nrepeat: none\n"
    for name, rot in (("r0", "false"), ("r90", "true"), ("r180", "180"), ("r270", "270")):
        text += f"{name}\n  rotate: {rot}\n  xy: 10, 20\n  size: 4, 6\n  orig: 4, 6\n  offset: 0, 0\n  index: -1\n"
    regions, _ = read_atlas(text)
    # content (cx, cy) with the region 4 wide, 6 tall -> expected page pixel
    want = {
        "r0": {(0, 0): (10, 20), (4, 0): (14, 20), (0, 6): (10, 26)},
        "r90": {(0, 0): (10, 24), (4, 0): (10, 20), (0, 6): (16, 24)},
        "r180": {(0, 0): (14, 26), (4, 0): (10, 26), (0, 6): (14, 20)},
        "r270": {(0, 0): (16, 20), (4, 0): (16, 24), (0, 6): (10, 20)},
    }
    for name, cases in want.items():
        rg = regions[name]
        for (cx, cy), exp in cases.items():
            got = render38.page_to_pixel(rg, cx, cy)
            if tuple(got) != exp:
                raise AssertionError(f"{name} ({cx},{cy}) -> {got}, want {exp}")
    # a real painting with 180/270 regions: meshes must hit their own opaque texels
    import numpy as np
    from PIL import Image, ImageDraw
    d = os.path.join(ROOT, "assets", "official", "game", "unpacked", "spinepainting", "nengdai_9_res")
    if not os.path.isdir(d):
        return "synthetic corners ok (nengdai_9 not extracted, real check skipped)"
    m = render38.open_painting(d)
    regions, pages = read_atlas(open(os.path.join(d, "nengdai_9.atlas"), encoding="utf-8").read())
    alpha = np.asarray(Image.open(os.path.join(d, "nengdai_9.png")).convert("RGBA"))[..., 3]
    worst = []
    for _, table in m.sk.skins:
        for si, atts in table.items():
            for a in atts.values():
                if a.type != "mesh" or regions[a.path].degrees not in (180, 270):
                    continue
                xs = [p[0] for p in a.page_uv]
                ys = [p[1] for p in a.page_uv]
                x0, y0 = int(min(xs)), int(min(ys))
                mask = Image.new("L", (int(max(xs)) - x0 + 2, int(max(ys)) - y0 + 2), 0)
                dr = ImageDraw.Draw(mask)
                for i in range(0, len(a.tris), 3):
                    dr.polygon([(a.page_uv[a.tris[i + k]][0] - x0, a.page_uv[a.tris[i + k]][1] - y0) for k in range(3)], fill=255)
                mk = np.asarray(mask) > 0
                cov = float((alpha[y0:y0 + mk.shape[0], x0:x0 + mk.shape[1]][mk[:alpha.shape[0] - y0, :alpha.shape[1] - x0]] > 8).mean())
                worst.append((cov, m.sk.slots[si].name))
    for name in ("yujin", "y_shou1", "zs"):
        cov = dict((n, c) for c, n in worst)[name]
        if cov < 0.9:
            raise AssertionError(f"{name} samples only {cov:.0%} opaque texels: wrong region rotation")
    return f"corners ok for 0/90/180/270; {len(worst)} rotated meshes in nengdai_9, min coverage {min(worst)[0]:.2f}"


def main():
    if "--record" in sys.argv:
        for k, v in chibi_hashes().items():
            print(f'    "{k}": "{v}",')
        return
    failed = 0
    checks = [("chibi bit-identical", check_chibi), ("JSON reader, synthetic", check_json_synthetic),
              ("JSON reader, beierfasite_g", check_json_painting), ("path constraint, synthetic", check_path_synthetic),
              ("path constraint, beierfasite_9", check_path_painting), ("expression overlay", check_overlay),
              ("model paths", check_model_paths), ("atlas rotation", check_atlas_rotation)]
    for name, fn in checks:
        t0 = time.time()
        try:
            msg = fn()
            print(f"PASS {name}: {msg} ({time.time() - t0:.1f}s)")
        except Exception as e:  # noqa: BLE001 - report every check
            failed += 1
            print(f"FAIL {name}: {type(e).__name__}: {e}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
