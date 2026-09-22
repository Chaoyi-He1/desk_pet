#!/usr/bin/env python3
"""Generate assets/belfast.png (placeholder card), assets/icon.png and assets/icon.ico.

The placeholder is shown until the user drops their own transparent Belfast picture
into assets/belfast.png. No copyrighted artwork is generated or bundled.
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ASSETS = os.path.join(ROOT, "assets")

FONT_CANDIDATES = [
    "/System/Library/Fonts/PingFang.ttc",
    "/System/Library/Fonts/Hiragino Sans GB.ttc",
    "/System/Library/Fonts/STHeiti Light.ttc",
    "/Library/Fonts/Arial Unicode.ttf",
    "C:/Windows/Fonts/msyh.ttc",
    "C:/Windows/Fonts/simhei.ttf",
    "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
    "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
]


def load_font(size):
    for path in FONT_CANDIDATES:
        if os.path.exists(path):
            try:
                return ImageFont.truetype(path, size), True
            except OSError:
                continue
    return ImageFont.load_default(), False


def make_placeholder(path):
    w, h = 240, 480
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Card
    d.rounded_rectangle((8, 8, w - 8, h - 8), radius=28, fill=(30, 34, 48, 215),
                        outline=(200, 205, 225, 255), width=3)
    # Simple maid headdress motif: silver arc + white band with two black dots
    d.pieslice((50, 60, 190, 200), 180, 360, fill=(217, 219, 230, 255))
    d.rounded_rectangle((58, 96, 182, 118), radius=10, fill=(245, 245, 250, 255))
    d.ellipse((84, 102, 96, 114), fill=(28, 28, 36, 255))
    d.ellipse((144, 102, 156, 114), fill=(28, 28, 36, 255))
    # Eyes
    d.ellipse((92, 140, 110, 164), fill=(58, 123, 213, 255))
    d.ellipse((130, 140, 148, 164), fill=(58, 123, 213, 255))
    d.ellipse((96, 144, 102, 150), fill=(255, 255, 255, 255))
    d.ellipse((134, 144, 140, 150), fill=(255, 255, 255, 255))

    font, cjk = load_font(19)
    small, _ = load_font(14)
    if cjk:
        lines = ["把透明背景的", "贝尔法斯特立绘", "保存为", "assets/belfast.png", "", "然后重新启动"]
    else:
        lines = ["Put a transparent", "Belfast PNG at", "assets/belfast.png", "", "then restart"]
    y = 232
    for i, text in enumerate(lines):
        f = small if text.startswith("assets/") else font
        bbox = d.textbbox((0, 0), text, font=f)
        tw = bbox[2] - bbox[0]
        d.text(((w - tw) / 2, y), text, font=f, fill=(240, 242, 250, 255))
        y += 30 if text else 14
    img.save(path)
    return img


def make_icon(png_path, ico_path):
    size = 64
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    # Teacup silhouette (Royal Navy maid motif). Drawn in white with dark outline so it
    # reads on both light and dark trays.
    d.rounded_rectangle((10, 22, 44, 50), radius=8, fill=(250, 250, 252, 255), outline=(40, 40, 52, 255), width=3)
    d.ellipse((40, 26, 56, 44), outline=(40, 40, 52, 255), width=3)
    d.rounded_rectangle((8, 50, 50, 58), radius=4, fill=(250, 250, 252, 255), outline=(40, 40, 52, 255), width=3)
    # Steam
    for x in (18, 27, 36):
        d.line((x, 8, x + 3, 14, x, 19), fill=(40, 40, 52, 255), width=2)
    img.resize((32, 32), Image.LANCZOS).save(png_path)
    img.save(ico_path, format="ICO", sizes=[(16, 16), (32, 32), (48, 48), (64, 64)])


def main():
    os.makedirs(ASSETS, exist_ok=True)
    target = os.path.join(ASSETS, "belfast.png")
    if os.path.exists(target) and "--force" not in sys.argv:
        print("assets/belfast.png exists, not overwriting (use --force)")
    else:
        make_placeholder(target)
        print("wrote", target)
    make_icon(os.path.join(ASSETS, "icon.png"), os.path.join(ASSETS, "icon.ico"))
    print("wrote icons")


if __name__ == "__main__":
    main()
