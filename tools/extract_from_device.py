#!/usr/bin/env python3
"""Copy Belfast's asset bundles from an Android device/emulator running Azur Lane and
unpack them.

The game keeps downloaded AssetBundles under
  /sdcard/Android/data/<package>/files/AssetBundles/<category>/<name>.ys
This script pulls every bundle whose name contains --match (default: beierfasite) into
OUT/bundles/, then dumps each bundle with UnityPy into OUT/unpacked/<category>/<name>/:
  Texture2D  -> <texture name>.png
  TextAsset  -> <name> (raw bytes; Spine .skel / .atlas end up here)
  Mesh       -> <name>.obj
  MonoBehaviour / GameObject layout -> <name>.json (type tree, when readable)
A summary.json lists what was found per category.

Usage: python3 tools/extract_from_device.py OUT [--package com.bilibili.azurlane] [--match beierfasite]
                                             [--only char,paintingface]
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

ADB = shutil.which("adb") or os.path.expanduser("~/Library/Android/sdk/platform-tools/adb")


def adb(*args, binary=False):
    r = subprocess.run([ADB, *args], capture_output=True)
    if r.returncode != 0:
        raise RuntimeError(f"adb {' '.join(args)} failed: {r.stderr.decode(errors='replace')}")
    return r.stdout if binary else r.stdout.decode(errors="replace")


def list_remote(root, match):
    out = adb("shell", f"find '{root}' -type f -iname '*{match}*' 2>/dev/null")
    return [l.strip() for l in out.splitlines() if l.strip()]


def safe(name):
    return "".join(c if c.isalnum() or c in "._-" else "_" for c in name) or "unnamed"


UNITY_VERSION = "2022.3.51f1"  # the game's engine version (seen in the APK's bundle headers)


def dump_bundle(path, outdir):
    import UnityPy

    # Some bundles have the engine version stripped from their header.
    UnityPy.config.FALLBACK_UNITY_VERSION = UNITY_VERSION
    try:
        env = UnityPy.load(path)
    except Exception as e:
        return {"errors": [f"load: {e}"]}
    found = {}
    os.makedirs(outdir, exist_ok=True)
    for obj in env.objects:
        t = obj.type.name
        try:
            if t == "Texture2D":
                d = obj.read()
                d.image.save(os.path.join(outdir, safe(d.m_Name) + ".png"))
            elif t == "TextAsset":
                d = obj.read()
                data = d.m_Script
                if isinstance(data, str):
                    data = data.encode("utf-8", "surrogateescape")
                with open(os.path.join(outdir, safe(d.m_Name)), "wb") as f:
                    f.write(data)
            elif t == "Mesh":
                d = obj.read()
                with open(os.path.join(outdir, safe(d.m_Name) + ".obj"), "w") as f:
                    f.write(d.export())
            elif t in ("MonoBehaviour", "GameObject", "RectTransform", "Transform", "Material"):
                tree = obj.read_typetree()
                name = tree.get("m_Name") or f"{t}_{obj.path_id}"
                with open(os.path.join(outdir, safe(f"{t}_{name}") + ".json"), "w", encoding="utf-8") as f:
                    json.dump(tree, f, ensure_ascii=False, indent=1, default=str)
            else:
                continue
            found[t] = found.get(t, 0) + 1
        except Exception as e:  # keep going; one odd object must not stop the dump
            found.setdefault("errors", []).append(f"{t}#{obj.path_id}: {e}")
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--package", default="com.bilibili.azurlane")
    ap.add_argument("--match", default="beierfasite")
    ap.add_argument("--only", default="", help="comma-separated categories to keep, e.g. char,paintingface")
    a = ap.parse_args()
    only = {c for c in a.only.split(",") if c}

    root = f"/sdcard/Android/data/{a.package}/files/AssetBundles"
    remote = list_remote(root, a.match)
    if only:
        remote = [r for r in remote if os.path.relpath(r, root).split("/")[0] in only]
    if not remote:
        print(f"no bundles matching '{a.match}' under {root}; has the game finished downloading?")
        return 1
    summary = {}
    for rp in sorted(remote):
        rel = os.path.relpath(rp, root)
        local = os.path.join(a.out, "bundles", rel)
        os.makedirs(os.path.dirname(local), exist_ok=True)
        adb("pull", rp, local)
        cat = rel.split("/")[0]
        stem = os.path.splitext(os.path.basename(rel))[0]
        info = dump_bundle(local, os.path.join(a.out, "unpacked", cat, stem))
        summary.setdefault(cat, {})[stem] = {"bytes": os.path.getsize(local), **info}
        print(f"{rel:60s} {os.path.getsize(local):>10}  {info}")
    with open(os.path.join(a.out, f"summary_{a.match}.json"), "w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=1)
    print(f"{len(remote)} bundles -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
