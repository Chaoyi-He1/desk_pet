#!/usr/bin/env python3
"""Upscale chibi texture atlases 2x with the Real-ESRGAN anime model, so pre-rendered
frames get real detail instead of blurry magnification.

  python3 tools/spine/upscale.py [MODEL_DIR ...]      (default: every model under
                                                       assets/official/game/unpacked/char)

For every atlas page <page>.png it writes <page>@2x.png next to it (skipped if present);
tools/spine/render38.py picks those up automatically. Needs PyTorch and the weights file
assets/official/models/RealESRGAN_x4plus_anime_6B.pth (github.com/xinntao/Real-ESRGAN,
BSD-3-Clause). The network (RRDBNet, 6 blocks) is defined here; the weights are loaded
with weights_only=True, so the file can only contain tensors.
"""
import glob
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WEIGHTS = os.path.join(ROOT, "assets", "official", "models", "RealESRGAN_x4plus_anime_6B.pth")
TILE, PAD = 192, 12


def build_net():
    import torch
    import torch.nn as nn
    import torch.nn.functional as F

    class RDB(nn.Module):
        def __init__(self, nf=64, gc=32):
            super().__init__()
            self.conv1 = nn.Conv2d(nf, gc, 3, 1, 1)
            self.conv2 = nn.Conv2d(nf + gc, gc, 3, 1, 1)
            self.conv3 = nn.Conv2d(nf + 2 * gc, gc, 3, 1, 1)
            self.conv4 = nn.Conv2d(nf + 3 * gc, gc, 3, 1, 1)
            self.conv5 = nn.Conv2d(nf + 4 * gc, nf, 3, 1, 1)
            self.act = nn.LeakyReLU(0.2, inplace=True)

        def forward(self, x):
            x1 = self.act(self.conv1(x))
            x2 = self.act(self.conv2(torch.cat((x, x1), 1)))
            x3 = self.act(self.conv3(torch.cat((x, x1, x2), 1)))
            x4 = self.act(self.conv4(torch.cat((x, x1, x2, x3), 1)))
            return self.conv5(torch.cat((x, x1, x2, x3, x4), 1)) * 0.2 + x

    class RRDB(nn.Module):
        def __init__(self, nf=64, gc=32):
            super().__init__()
            self.rdb1, self.rdb2, self.rdb3 = RDB(nf, gc), RDB(nf, gc), RDB(nf, gc)

        def forward(self, x):
            return self.rdb3(self.rdb2(self.rdb1(x))) * 0.2 + x

    class RRDBNet(nn.Module):
        def __init__(self, nb=6, nf=64, gc=32):
            super().__init__()
            self.conv_first = nn.Conv2d(3, nf, 3, 1, 1)
            self.body = nn.Sequential(*[RRDB(nf, gc) for _ in range(nb)])
            self.conv_body = nn.Conv2d(nf, nf, 3, 1, 1)
            self.conv_up1 = nn.Conv2d(nf, nf, 3, 1, 1)
            self.conv_up2 = nn.Conv2d(nf, nf, 3, 1, 1)
            self.conv_hr = nn.Conv2d(nf, nf, 3, 1, 1)
            self.conv_last = nn.Conv2d(nf, 3, 3, 1, 1)
            self.act = nn.LeakyReLU(0.2, inplace=True)

        def forward(self, x):
            feat = self.conv_first(x)
            feat = feat + self.conv_body(self.body(feat))
            feat = self.act(self.conv_up1(F.interpolate(feat, scale_factor=2, mode="nearest")))
            feat = self.act(self.conv_up2(F.interpolate(feat, scale_factor=2, mode="nearest")))
            return self.conv_last(self.act(self.conv_hr(feat)))

    net = RRDBNet()
    state = torch.load(WEIGHTS, map_location="cpu", weights_only=True)
    net.load_state_dict(state.get("params_ema", state), strict=True)
    dev = "mps" if torch.backends.mps.is_available() else "cpu"
    return net.eval().to(dev), dev


def sr4(net, dev, rgb):
    """rgb: HxWx3 float32 [0,1] -> 4H x 4W x 3, processed in overlapping tiles."""
    import torch
    h, w, _ = rgb.shape
    out = np.zeros((h * 4, w * 4, 3), dtype=np.float32)
    with torch.no_grad():
        for y in range(0, h, TILE):
            for x in range(0, w, TILE):
                y0, x0 = max(0, y - PAD), max(0, x - PAD)
                y1, x1 = min(h, y + TILE + PAD), min(w, x + TILE + PAD)
                t = torch.from_numpy(np.ascontiguousarray(rgb[y0:y1, x0:x1].transpose(2, 0, 1)))[None].to(dev)
                r = net(t).clamp_(0, 1)[0].cpu().numpy().transpose(1, 2, 0)
                ty, tx = (y - y0) * 4, (x - x0) * 4
                th, tw = min(TILE, h - y) * 4, min(TILE, w - x) * 4
                out[y * 4:y * 4 + th, x * 4:x * 4 + tw] = r[ty:ty + th, tx:tx + tw]
    return out


def bleed(rgba, iterations=8):
    """Spread colour into transparent texels so upscaling does not pull in dark fringes."""
    rgb = rgba[..., :3].copy()
    a = rgba[..., 3] > 0.02
    for _ in range(iterations):
        if a.all():
            break
        acc = np.zeros_like(rgb)
        cnt = np.zeros(a.shape, dtype=np.float32)
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            sa = np.roll(a, (dy, dx), (0, 1))
            acc += np.roll(rgb, (dy, dx), (0, 1)) * sa[..., None]
            cnt += sa
        fill = (~a) & (cnt > 0)
        rgb[fill] = acc[fill] / cnt[fill][:, None]
        a = a | fill
    return rgb


def upscale_page(net, dev, src, dst):
    img = np.asarray(Image.open(src).convert("RGBA"), dtype=np.float32) / 255.0
    rgb4 = sr4(net, dev, bleed(img))
    alpha4 = sr4(net, dev, np.repeat(img[..., 3:4], 3, axis=2)).mean(axis=2, keepdims=True)
    out4 = Image.fromarray(np.clip(np.concatenate([rgb4, alpha4], 2) * 255 + 0.5, 0, 255).astype(np.uint8), "RGBA")
    h, w = img.shape[:2]
    out4.resize((w * 2, h * 2), Image.LANCZOS).save(dst)


def main():
    dirs = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, "assets", "official", "game", "unpacked", "char", "*")))
    net, dev = build_net()
    print("device:", dev)
    for d in dirs:
        name = os.path.basename(os.path.normpath(d))
        atlas = os.path.join(d, name + ".atlas")
        if not os.path.exists(atlas):
            continue
        pages = [l.strip() for l in open(atlas, encoding="utf-8") if l.strip().lower().endswith(".png")]
        for p in pages:
            src = os.path.join(d, p)
            dst = os.path.join(d, p[:-4] + "@2x.png")
            if os.path.exists(dst) or not os.path.exists(src):
                continue
            upscale_page(net, dev, src, dst)
            print(f"{name}: {p} -> {os.path.basename(dst)}", flush=True)


if __name__ == "__main__":
    main()
