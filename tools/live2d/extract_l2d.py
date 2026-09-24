#!/usr/bin/env python3
"""Rebuild a standard Live2D Cubism model folder from an Azur Lane live2d/<name> bundle.

  python3 tools/live2d/extract_l2d.py BUNDLE OUT_DIR

The game ships Live2D models as Cubism-for-Unity prefabs. This writes:
  <name>.moc3            from the CubismMoc component
  texture_00.png ...     textures
  <name>.physics3.json   the physics TextAsset
  motions/<clip>.motion3.json   Unity AnimationClips converted to Cubism motions
  <name>.model3.json     references to all of the above; motion groups by clip name
AnimationClips are stored as Unity "streamed" (piecewise cubic) and "constant" curves bound
to Parameters/<id>.Value and Parts/<id>.Opacity by CRC32 path hashes; they are resampled at
the clip's sample rate into linear motion3 segments.
"""
import json
import os
import struct
import sys
import zlib

import UnityPy

UnityPy.config.FALLBACK_UNITY_VERSION = "2022.3.51f1"
ATTR_VALUE = zlib.crc32(b"Value")
ATTR_OPACITY = zlib.crc32(b"Opacity")


def read_streamed(words, curve_count):
    """Unity StreamedClip -> {curve_index: [(time, c0, c1, c2, c3), ...]} sorted by time."""
    raw = struct.pack(f"<{len(words)}I", *words)
    pos, n = 0, len(raw)
    keys = {}
    while pos + 8 <= n:
        t, count = struct.unpack_from("<fi", raw, pos)
        pos += 8
        for _ in range(count):
            idx, c0, c1, c2, c3 = struct.unpack_from("<i4f", raw, pos)
            pos += 20
            keys.setdefault(idx, []).append((t, c0, c1, c2, c3))
    return keys


def eval_streamed(key_list, t):
    """Value at time t: the cubic of the last key at or before t."""
    k = key_list[0]
    for cand in key_list:
        if cand[0] <= t:
            k = cand
        else:
            break
    dt = max(0.0, t - k[0]) if k[0] > -1e30 else 0.0
    return ((k[1] * dt + k[2]) * dt + k[3]) * dt + k[4]


def transform_paths(env):
    names, parent = {}, {}
    for o in env.objects:
        if o.type.name == "GameObject":
            names[o.path_id] = o.read_typetree()["m_Name"]
    for o in env.objects:
        if o.type.name == "Transform":
            t = o.read_typetree()
            parent[o.path_id] = (t["m_GameObject"]["m_PathID"], t["m_Father"]["m_PathID"])
    out = []
    for tid in parent:
        parts, cur = [], tid
        while cur in parent:
            go, fa = parent[cur]
            parts.append(names.get(go, ""))
            cur = fa
        full = list(reversed(parts))
        if len(full) >= 2:
            out.append("/".join(full[1:]))  # relative to the Animator root
    return out


def clip_to_motion(clip, hash_to_target):
    mc = clip["m_MuscleClip"]
    data = mc["m_Clip"]["data"]
    streamed = data["m_StreamedClip"]
    dense = data["m_DenseClip"]
    const = data["m_ConstantClip"]["data"]
    bindings = clip["m_ClipBindingConstant"]["genericBindings"]
    rate = float(clip["m_SampleRate"]) or 30.0
    start, stop = mc["m_StartTime"], mc["m_StopTime"]
    duration = max(0.0, stop - start)
    n_str = streamed["curveCount"]
    n_dense = dense["m_CurveCount"]
    skeys = read_streamed(streamed["data"], n_str)
    frames = max(1, int(round(duration * rate)))
    times = [start + i / rate for i in range(frames + 1)]
    curves = []
    for ci, b in enumerate(bindings):
        target = hash_to_target.get((b["path"], b["attribute"]))
        if target is None:
            continue
        kind, ident = target
        if ci < n_str:
            kl = skeys.get(ci)
            if not kl:
                continue
            vals = [eval_streamed(kl, t) for t in times]
        elif ci < n_str + n_dense:
            fc = dense["m_FrameCount"]
            arr = dense["m_SampleArray"]
            j = ci - n_str
            vals = [arr[min(fc - 1, int(round((t - dense["m_BeginTime"]) * dense["m_SampleRate"]))) * n_dense + j] for t in times]
        else:
            vals = [const[ci - n_str - n_dense]] * 2
        ts = times if len(vals) == len(times) else [start, stop]
        # drop redundant samples on straight runs to keep files small
        pts = [(ts[0] - start, vals[0])]
        for k in range(1, len(vals) - 1):
            if abs(vals[k] - vals[k - 1]) > 1e-5 or abs(vals[k + 1] - vals[k]) > 1e-5:
                pts.append((ts[k] - start, vals[k]))
        pts.append((ts[-1] - start, vals[-1]))
        seg = [round(pts[0][0], 4), round(pts[0][1], 5)]
        for t, v in pts[1:]:
            seg += [0, round(t, 4), round(v, 5)]
        curves.append({"Target": kind, "Id": ident, "Segments": seg})
    total_seg = sum((len(c["Segments"]) - 2) // 3 for c in curves)
    total_pts = sum(1 + (len(c["Segments"]) - 2) // 3 for c in curves)
    loop = bool(mc.get("m_LoopTime"))
    return {
        "Version": 3,
        "Meta": {"Duration": round(duration, 4), "Fps": rate, "Loop": loop, "AreBeziersRestricted": True,
                 "CurveCount": len(curves), "TotalSegmentCount": total_seg, "TotalPointCount": total_pts,
                 "UserDataCount": 0, "TotalUserDataSize": 0},
        "Curves": curves,
    }


def main():
    bundle, out = sys.argv[1], sys.argv[2]
    name = os.path.basename(bundle)
    os.makedirs(os.path.join(out, "motions"), exist_ok=True)
    env = UnityPy.load(bundle)
    hash_to_target = {}
    for p in transform_paths(env):
        h = zlib.crc32(p.encode())
        if p.startswith("Parameters/"):
            hash_to_target[(h, ATTR_VALUE)] = ("Parameter", p.split("/", 1)[1])
        elif p.startswith("Parts/"):
            hash_to_target[(h, ATTR_OPACITY)] = ("PartOpacity", p.split("/", 1)[1])
    textures, physics, motions = [], None, {}
    for o in env.objects:
        tn = o.type.name
        if tn == "MonoBehaviour":
            d = o.read()
            if d.m_Script and d.m_Script.read().m_ClassName == "CubismMoc":
                with open(os.path.join(out, name + ".moc3"), "wb") as f:
                    f.write(bytes(o.read_typetree()["_bytes"]))
        elif tn == "Texture2D":
            d = o.read()
            textures.append(d.m_Name)
            d.image.save(os.path.join(out, d.m_Name + ".png"))
        elif tn == "TextAsset":
            d = o.read()
            if d.m_Name.endswith("physics3"):
                physics = name + ".physics3.json"
                data = d.m_Script if isinstance(d.m_Script, str) else d.m_Script.decode("utf-8")
                open(os.path.join(out, physics), "w", encoding="utf-8").write(data)
        elif tn == "AnimationClip":
            clip = o.read_typetree()
            m = clip_to_motion(clip, hash_to_target)
            if m["Curves"]:
                fn = f"motions/{clip['m_Name']}.motion3.json"
                json.dump(m, open(os.path.join(out, fn), "w"), separators=(",", ":"))
                motions[clip["m_Name"]] = fn
    textures.sort()
    model = {
        "Version": 3,
        "FileReferences": {
            "Moc": name + ".moc3",
            "Textures": [t + ".png" for t in textures],
            "Motions": {k: [{"File": v}] for k, v in sorted(motions.items())},
        },
        "Groups": [],
    }
    if physics:
        model["FileReferences"]["Physics"] = physics
    json.dump(model, open(os.path.join(out, name + ".model3.json"), "w", encoding="utf-8"), ensure_ascii=False, indent=1)
    print(f"{name}: textures {textures}, physics {'yes' if physics else 'no'}, motions {sorted(motions)}")


if __name__ == "__main__":
    main()
