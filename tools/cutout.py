#!/usr/bin/env python3
"""Cut a character out of an illustration with a background (rembg, anime model).

Usage: python3 tools/cutout.py <input.jpg> <output.png> [--model isnet-anime] [--margin 8]

The result is cropped to the character's bounding box (plus margin) and saved as a
transparent PNG. Only the largest connected foreground region is kept, which drops
detached blobs such as site watermarks.
"""
import argparse
import io
import sys

import numpy as np
from PIL import Image


def largest_component(mask):
    """Keep only the largest 4-connected region of a boolean mask (BFS, no scipy)."""
    h, w = mask.shape
    labels = np.zeros((h, w), dtype=np.int32)
    best_label, best_size = 0, 0
    label = 0
    ys, xs = np.nonzero(mask)
    for sy, sx in zip(ys, xs):
        if labels[sy, sx]:
            continue
        label += 1
        stack = [(sy, sx)]
        labels[sy, sx] = label
        size = 0
        while stack:
            y, x = stack.pop()
            size += 1
            for ny, nx in ((y - 1, x), (y + 1, x), (y, x - 1), (y, x + 1)):
                if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not labels[ny, nx]:
                    labels[ny, nx] = label
                    stack.append((ny, nx))
        if size > best_size:
            best_size, best_label = size, label
    return labels == best_label


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("input")
    ap.add_argument("output")
    ap.add_argument("--model", default="isnet-anime")
    ap.add_argument("--margin", type=int, default=8)
    ap.add_argument("--matting", action="store_true", help="enable alpha matting (slower)")
    args = ap.parse_args()

    from rembg import new_session, remove

    src = Image.open(args.input).convert("RGB")
    session = new_session(args.model)
    kwargs = {}
    if args.matting:
        kwargs = dict(alpha_matting=True, alpha_matting_foreground_threshold=240,
                      alpha_matting_background_threshold=10, alpha_matting_erode_size=8)
    out = remove(src, session=session, post_process_mask=True, **kwargs).convert("RGBA")

    arr = np.array(out)
    alpha = arr[:, :, 3]
    solid = alpha > 40
    if solid.any():
        keep = largest_component(solid)
        # Keep soft edges next to the main region: dilate the kept mask by a few px.
        from PIL import ImageFilter
        keep_img = Image.fromarray((keep * 255).astype(np.uint8)).filter(ImageFilter.MaxFilter(9))
        keep = np.array(keep_img) > 0
        arr[:, :, 3] = np.where(keep, alpha, 0)
        ys, xs = np.nonzero(arr[:, :, 3] > 8)
        m = args.margin
        y0, y1 = max(0, ys.min() - m), min(arr.shape[0], ys.max() + 1 + m)
        x0, x1 = max(0, xs.min() - m), min(arr.shape[1], xs.max() + 1 + m)
        arr = arr[y0:y1, x0:x1]
    Image.fromarray(arr).save(args.output)
    print(f"wrote {args.output} {arr.shape[1]}x{arr.shape[0]}")


if __name__ == "__main__":
    main()
