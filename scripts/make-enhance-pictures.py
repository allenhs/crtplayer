#!/usr/bin/env python3
"""Pictures for the Enhance test clips (2.13): detailed, with hard edges at many angles,
smooth gradients and fine texture. Usage: make-enhance-pictures.py <output-dir>"""
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

out = sys.argv[1]


def scene(w, h, seed):
    r = np.random.default_rng(seed)
    y, x = np.mgrid[0:h, 0:w].astype(float)
    base = np.stack([110 + 70 * np.sin(x / w * 5 + seed) + 40 * np.cos(y / h * 7),
                     120 + 60 * np.sin(y / h * 4 + 1 + seed) + 30 * np.cos((x + y) / w * 6),
                     130 + 70 * np.cos(x / w * 3 + 2) + 40 * np.sin(y / h * 9 + seed)], axis=2)
    im = Image.fromarray(np.clip(base, 0, 255).astype('uint8'))
    d = ImageDraw.Draw(im)
    for _ in range(140):
        x0, y0 = int(r.integers(0, w)), int(r.integers(0, h))
        s = int(r.integers(h // 40, h // 5))
        col = tuple(int(v) for v in r.integers(20, 240, 3))
        k = int(r.integers(0, 4))
        if k == 0: d.ellipse([x0, y0, x0 + s, y0 + s // 2 + 8], fill=col)
        elif k == 1: d.polygon([(x0, y0), (x0 + s, y0 + s // 3), (x0 + s // 3, y0 + s)], fill=col)
        elif k == 2: d.line([(x0, y0), (x0 + s, y0 + int(r.integers(-s, s)))], fill=col, width=int(r.integers(1, 5)))
        else: d.rectangle([x0, y0, x0 + s // 2, y0 + s // 2], outline=col, width=2)
    a = np.asarray(im, dtype=float)
    tex = r.normal(0, 1, (h, w))
    tex = np.asarray(Image.fromarray(((tex - tex.min()) / np.ptp(tex) * 255).astype('uint8')).filter(ImageFilter.GaussianBlur(1.2)), dtype=float)
    a += ((tex - tex.mean()) * 1.6)[..., None]
    return Image.fromarray(np.clip(a, 0, 255).astype('uint8'))


scene(1920, 1080, 1).save(f'{out}/enh_detail.png')   # the full-size original
scene(2400, 540, 2).save(f'{out}/enh_pan.png')       # a wide background to pan across
scene(960, 540, 3).save(f'{out}/enh_other.png')      # another scene (a cut; a textured moving object)
