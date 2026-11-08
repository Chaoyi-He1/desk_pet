#!/usr/bin/env python3
"""Export the game's JUUS chat sprites for the pet's chat box into assets/ui/juus/.

The sprites live in two UI bundles of the game's download cache on an Android device or
emulator running Azur Lane:
  /sdcard/Android/data/<package>/files/AssetBundles/ui/dorm3dchatui     (the sprites)
  /sdcard/Android/data/<package>/files/AssetBundles/ui/instagramchatui  (their atlas)
Both are pulled with adb and loaded together with UnityPy (a sprite finds its atlas in the
sibling bundle; the headers carry no engine version, so the game's is supplied).

Usage: python3 tools/extract_ui.py [OUT] [--package com.bilibili.azurlane] [--from-dir DIR]
  OUT defaults to assets/ui/juus. --from-dir uses bundles pulled earlier instead of adb.
Set ADB to the adb binary when it is not on PATH (MuMu: <MuMu>\\nx_main\\adb.exe, after
`adb connect 127.0.0.1:16384`).
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile

UNITY_VERSION = "2022.3.51f1"  # the game's engine version (seen in the APK's bundle headers)
BUNDLES = ["dorm3dchatui", "instagramchatui"]
# pet file name -> sprite name in the game
SPRITES = {
    "bottom_bar.png": "bottomBar",        # the white input bar
    "send.png": "oa_send",                # the paper plane
    "avatar_bg.png": "charaBg",           # the white disc behind an avatar
    "reply_bg.png": "replyBg",            # the grey pill (the 语音 button)
    "ship_message.png": "shipMessage",    # a ship's message box
    "player_reply.png": "playerReplyFrame",  # the player's blue reply box
}


def pull(package, out_dir):
    adb = os.environ.get("ADB") or shutil.which("adb")
    if not adb:
        sys.exit("adb not found: put it on PATH or set ADB")
    root = f"/sdcard/Android/data/{package}/files/AssetBundles/ui"
    paths = []
    for b in BUNDLES:
        dst = os.path.join(out_dir, b)
        r = subprocess.run([adb, "pull", f"{root}/{b}", dst], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"adb pull {b} failed: {r.stderr.strip()}")
        paths.append(dst)
    return paths


def export(bundle_paths, out):
    import UnityPy

    UnityPy.config.FALLBACK_UNITY_VERSION = UNITY_VERSION
    env = UnityPy.load(*bundle_paths)
    wanted = {v: k for k, v in SPRITES.items()}
    os.makedirs(out, exist_ok=True)
    done = set()
    for obj in env.objects:
        if obj.type.name != "Sprite":
            continue
        data = obj.read()
        name = wanted.get(data.m_Name)
        if not name or name in done:
            continue
        img = data.image
        img.save(os.path.join(out, name))
        done.add(name)
        print(f"{name}  {img.width}x{img.height}  <- {data.m_Name}")
    missing = set(SPRITES) - done
    if missing:
        sys.exit("not found in the bundles: " + ", ".join(sorted(missing)))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("out", nargs="?", default=os.path.join("assets", "ui", "juus"))
    ap.add_argument("--package", default="com.bilibili.azurlane")
    ap.add_argument("--from-dir", help="folder holding the pulled bundles instead of adb")
    a = ap.parse_args()
    if a.from_dir:
        paths = [os.path.join(a.from_dir, b) for b in BUNDLES]
        export(paths, a.out)
        return
    with tempfile.TemporaryDirectory() as tmp:
        export(pull(a.package, tmp), a.out)


if __name__ == "__main__":
    main()
