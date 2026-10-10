#!/usr/bin/env python3
"""Pictures for the stand-in video site of the browser's tests (2.18): painted scenes that read like video
thumbnails (640x360, t00..t47.jpg), and channel pictures (176x176, a00..a11.jpg). make-web-thumbs.py OUT_DIR"""
import math, os, random, sys
from PIL import Image, ImageDraw, ImageFilter

W, H = 640, 360
out = sys.argv[1]
os.makedirs(out, exist_ok=True)

def lerp(a, b, t): return tuple(int(a[i] + (b[i] - a[i]) * t) for i in range(3))
def vgrad(img, top, bottom, y0=0, y1=H):
    d = ImageDraw.Draw(img)
    for y in range(y0, y1):
        d.line([(0, y), (img.width, y)], fill=lerp(top, bottom, (y - y0) / max(1, y1 - y0 - 1)))

PALETTES = [((255, 140, 66), (64, 30, 90)), ((255, 94, 120), (20, 24, 70)), ((120, 200, 255), (18, 40, 90)),
            ((255, 210, 120), (150, 60, 40)), ((150, 255, 200), (10, 60, 70)), ((250, 180, 255), (50, 20, 90)),
            ((255, 236, 170), (60, 110, 160)), ((255, 120, 60), (30, 10, 30))]

def sunset(r, pal):
    im = Image.new('RGB', (W, H)); vgrad(im, pal[1], pal[0], 0, int(H * 0.75))
    d = ImageDraw.Draw(im)
    sx, sy, sr = r.randint(180, 460), r.randint(130, 200), r.randint(40, 70)
    d.ellipse([sx - sr, sy - sr, sx + sr, sy + sr], fill=lerp(pal[0], (255, 255, 230), 0.6))
    for layer in range(4):
        c = lerp(pal[1], (10, 10, 20), 0.35 + layer * 0.18)
        base = int(H * (0.55 + layer * 0.1)); pts = [(0, H)]
        x = 0
        while x <= W:
            pts.append((x, base - r.randint(0, 70 - layer * 12))); x += r.randint(40, 90)
        pts += [(W, H)]
        d.polygon(pts, fill=c)
    return im

def synth(r, pal):
    im = Image.new('RGB', (W, H)); vgrad(im, (20, 6, 40), pal[0], 0, H // 2)
    d = ImageDraw.Draw(im); vgrad(im, (25, 8, 45), (5, 2, 15), H // 2, H)
    cx, cy, cr = W // 2, H // 2 - 10, 90
    for y in range(cy - cr, cy):
        w = int(math.sqrt(max(0, cr * cr - (y - cy) ** 2)))
        if (y - (cy - cr)) % 14 < 10 or y < cy - 40: d.line([(cx - w, y), (cx + w, y)], fill=lerp((255, 230, 90), (255, 60, 140), (y - (cy - cr)) / cr))
    for i in range(-12, 13): d.line([(cx + i * 22, H // 2), (cx + i * 140, H)], fill=(255, 60, 200), width=2)
    y = H // 2; step = 6
    while y < H: d.line([(0, y), (W, y)], fill=(255, 60, 200), width=2); y += step; step = int(step * 1.35) + 1
    return im

def city(r, pal):
    im = Image.new('RGB', (W, H)); vgrad(im, (8, 10, 30), pal[1], 0, H)
    d = ImageDraw.Draw(im)
    for _ in range(120): x, y = r.randint(0, W), r.randint(0, H // 2); d.point((x, y), fill=(220, 220, 255))
    d.ellipse([480, 40, 540, 100], fill=(240, 236, 210))
    for layer in range(2):
        x = -10
        while x < W:
            bw, bh = r.randint(30, 80), r.randint(90, 250 - layer * 60)
            col = (18 + layer * 12, 20 + layer * 12, 34 + layer * 14)
            d.rectangle([x, H - bh, x + bw, H], fill=col)
            for wy in range(H - bh + 8, H - 6, 12):
                for wx in range(x + 5, x + bw - 6, 10):
                    if r.random() < 0.35: d.rectangle([wx, wy, wx + 4, wy + 5], fill=lerp(pal[0], (255, 240, 180), 0.5))
            x += bw + r.randint(0, 8)
    return im

def ocean(r, pal):
    im = Image.new('RGB', (W, H)); vgrad(im, pal[1], pal[0], 0, int(H * 0.58)); vgrad(im, lerp(pal[1], (0, 20, 40), 0.4), (2, 10, 25), int(H * 0.58), H)
    d = ImageDraw.Draw(im); hy = int(H * 0.58); sx = r.randint(200, 440)
    d.ellipse([sx - 34, hy - 70, sx + 34, hy - 2], fill=lerp(pal[0], (255, 255, 230), 0.7))
    for i in range(30):
        y = hy + 4 + i * 6; w = 80 - i * 2 + r.randint(-10, 10)
        d.line([(sx - w, y), (sx + w, y)], fill=lerp(pal[0], (255, 255, 230), 0.5), width=2)
    return im

def bokeh(r, pal):
    im = Image.new('RGB', (W, H)); vgrad(im, pal[1], lerp(pal[1], (0, 0, 0), 0.6))
    lay = Image.new('RGB', (W, H)); d = ImageDraw.Draw(lay)
    for _ in range(40):
        x, y, s = r.randint(0, W), r.randint(0, H), r.randint(10, 60)
        d.ellipse([x - s, y - s, x + s, y + s], fill=lerp(pal[0], (255, 255, 255), r.random() * 0.5))
    lay = lay.filter(ImageFilter.GaussianBlur(9))
    return Image.blend(im, lay, 0.55)

def tvset(r, pal):
    im = Image.new('RGB', (W, H)); vgrad(im, lerp(pal[0], (40, 40, 40), 0.3), lerp(pal[1], (0, 0, 0), 0.4))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([170, 50, 470, 300], 34, fill=(60, 44, 32)); d.rounded_rectangle([190, 70, 410, 260], 30, fill=(20, 20, 24))
    d.rounded_rectangle([200, 80, 400, 250], 26, fill=lerp(pal[0], (120, 200, 255), 0.5))
    for y in range(82, 250, 4): d.line([(204, y), (396, y)], fill=lerp(pal[0], (40, 60, 90), 0.5))
    for k in range(3): d.ellipse([425, 90 + k * 50, 455, 120 + k * 50], fill=(30, 30, 30))
    d.rectangle([220, 300, 240, 330], fill=(40, 30, 24)); d.rectangle([400, 300, 420, 330], fill=(40, 30, 24))
    return im

def forest(r, pal):
    im = Image.new('RGB', (W, H)); vgrad(im, lerp(pal[0], (220, 230, 240), 0.5), lerp(pal[1], (200, 210, 220), 0.3))
    d = ImageDraw.Draw(im)
    for layer in range(4):
        c = lerp((180, 190, 200), (12, 30, 24), 0.25 + layer * 0.25)
        for _ in range(16):
            x = r.randint(-20, W + 20); h = r.randint(90, 160) + layer * 25; y = H - 50 + layer * 12
            d.polygon([(x, y - h), (x - h // 4, y), (x + h // 4, y)], fill=c)
        d.rectangle([0, H - 50 + layer * 12, W, H], fill=c)
    return im

def space(r, pal):
    im = Image.new('RGB', (W, H), (4, 4, 12)); d = ImageDraw.Draw(im)
    for _ in range(300): x, y = r.randint(0, W), r.randint(0, H); b = r.randint(80, 255); d.point((x, y), fill=(b, b, b))
    px, py, pr = r.randint(220, 420), r.randint(140, 220), r.randint(70, 110)
    for i in range(pr, 0, -1): d.ellipse([px - i, py - i, px + i, py + i], fill=lerp(pal[0], pal[1], i / pr))
    d.ellipse([px - pr * 2, py - pr // 4, px + pr * 2, py + pr // 4], outline=lerp(pal[0], (255, 255, 255), 0.4), width=4)
    return im

KINDS = [sunset, synth, city, ocean, bokeh, tvset, forest, space]
for i in range(48):
    r = random.Random(i * 7919)
    pal = PALETTES[(i * 3 + i // 8 * 5) % len(PALETTES)]
    im = KINDS[i % len(KINDS)](r, pal).filter(ImageFilter.GaussianBlur(0.6))
    im.save(os.path.join(out, 't%02d.jpg' % i), quality=86)
for i in range(12):
    r = random.Random(1000 + i); pal = PALETTES[i % len(PALETTES)]
    im = Image.new('RGB', (176, 176)); vgrad(im, pal[0], pal[1], 0, 176)
    d = ImageDraw.Draw(im)
    shape = i % 3
    if shape == 0: d.ellipse([48, 48, 128, 128], fill=lerp(pal[0], (255, 255, 255), 0.6))
    elif shape == 1: d.rounded_rectangle([40, 52, 136, 124], 16, fill=(24, 24, 30)); d.rounded_rectangle([50, 62, 126, 114], 12, fill=lerp(pal[0], (255, 255, 255), 0.4))
    else: d.polygon([(88, 36), (140, 132), (36, 132)], fill=lerp(pal[1], (255, 255, 255), 0.7))
    im.save(os.path.join(out, 'a%02d.jpg' % i), quality=88)
