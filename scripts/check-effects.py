#!/usr/bin/env python3
"""Measures the effect screenshots written by tests/automation/effects.txt (needs Pillow + numpy)."""
import sys, pathlib
import numpy as np
from PIL import Image

out = pathlib.Path(sys.argv[1]); fails = 0
def L(n): return np.asarray(Image.open(out / n).convert('L'), float)
def RGB(n): return np.asarray(Image.open(out / n).convert('RGB'), float)
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

h, w = L('fx_consumer.png').shape
# Picture area (4:3 inside the video area) and the card's grey-stripe band (rows 35-62 % of the card).
pw = round(h * 4 / 3); x0 = (w - pw) // 2
stripes = (slice(int(h * .37), int(h * .60)), slice(x0 + 5, x0 + pw - 5))
chroma = lambda a: np.abs(a - a.mean(axis=2, keepdims=True)).mean(axis=2)
ref_c, ld_c = chroma(RGB('fx_consumer.png'))[stripes].mean(), chroma(RGB('fx_laserdisc.png'))[stripes].mean()
check('rainbow crosstalk adds false colour to grey stripes', ld_c > ref_c * 1.3, f'{ref_c:.1f} -> {ld_c:.1f}')
shots = [L(n) for n in ('fx_laserdisc.png', 'fx_laserdisc_later.png', 'fx_laserdisc_later2.png', 'fx_laserdisc_later3.png')]
changes = [(np.abs(shots[i] - shots[j]) > 8).mean() for i in range(4) for j in range(i + 1, 4)]
check('dot crawl / rainbow animate on a paused frame', max(changes) > 0.02,
      'pixels changed between moments: ' + ', '.join(f'{c * 100:.1f}%' for c in changes))
bottom = (slice(int(h * .955), int(h * .99)), slice(x0, x0 + pw))
middle = (slice(int(h * .80), int(h * .835)), slice(x0, x0 + pw))
d_bot = np.abs(L('fx_vhs_home.png') - L('fx_consumer.png'))[bottom].mean()
d_mid = np.abs(L('fx_vhs_home.png') - L('fx_consumer.png'))[middle].mean()
check('head switching disturbs only the bottom of the picture', d_bot > d_mid * 1.5, f'bottom diff {d_bot:.1f} vs mid {d_mid:.1f}')
rot = np.abs(L('fx_laserrot.png') - L('fx_laserdisc.png'))
check('laser rot adds sparse speckles', 0.0005 < (rot > 60).mean() < 0.05, f'{(rot > 60).mean() * 100:.2f}% pixels')
col = slice(x0 + pw // 2 - 20, x0 + pw // 2 + 20)
profiles = {}
for n in ['scan_0_soft', 'scan_1_sharp', 'scan_2_dynamic', 'scan_3_interlaced_a', 'scan_4_vga', 'scan_5_pixel',
          'scan_6_geom', 'scan_7_lottes', 'scan_8_easymode', 'scan_9_hyllian', 'scan_10_mame']:
    profiles[n] = L(n + '.png')[int(h * .05):int(h * .30), col].mean(axis=1)
names = list(profiles)
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        if names[i] == 'scan_0_soft' and names[j] == 'scan_3_interlaced_a':
            continue   # same beam shape; interlacing is checked separately
        d = np.abs(profiles[names[i]] - profiles[names[j]]).mean()
        check(f'scanline styles differ: {names[i]} vs {names[j]}', d > 1.0, f'mean row diff {d:.1f}')
fields = [L(f'scan_3_interlaced_{k}.png')[int(h * .05):int(h * .30), col].mean(axis=1) for k in 'abcd']
diffs = [float(np.abs(fields[i] - fields[j]).mean()) for i in range(4) for j in range(i + 1, 4)]
check('interlaced style alternates field position', max(diffs) > 2.0,
      'row-profile differences between moments: ' + ', '.join(f'{d:.1f}' for d in diffs))
pix = L('scan_5_pixel.png')[int(h * .08):int(h * .12), x0 + 20:x0 + pw - 20].mean(axis=0)
spec = np.abs(np.fft.rfft(pix - pix.mean())); peak = spec[5:].argmax() + 5
check('pixel beam adds a horizontal pixel structure', spec[peak] > 5 * np.median(spec[5:]), f'peak/median {spec[peak] / np.median(spec[5:]):.0f}')
vga = L('scan_4_vga.png')[int(h * .05):int(h * .30), col].mean(axis=1)
dark = vga < vga.max() * 0.8; starts = np.flatnonzero(dark[1:] & ~dark[:-1]); gaps = np.diff(starts)
check('VGA double-scan gaps are regular', len(gaps) > 10 and gaps.max() <= 4, f'gap spacing {gaps.min()}-{gaps.max()} px')
# Beam behaviour on the grey ramp: gap depth (1 - gap/peak) per band, 160 lines.
def band_depths(n, rows):
    im = RGB(n).max(axis=2); hh, ww = im.shape
    pw = round(hh * 4 / 3); x0 = (ww - pw) // 2; bw = pw / 5; per = hh / 160
    r0, r1 = int(hh * rows[0]), int(hh * rows[1]); out = []
    for i in range(5):
        xc = int(x0 + bw * (i + 0.5)); prof = im[r0:r1, xc - 40:xc + 40].mean(axis=1)
        mx = [prof[int(k * per):int((k + 1) * per) + 1].max() for k in range(int(len(prof) / per) - 1)]
        mn = [prof[int(k * per):int((k + 1) * per) + 1].min() for k in range(int(len(prof) / per) - 1)]
        out.append((np.median(mx) - np.median(mn)) / max(np.median(mx), 1))
    return out
for t, name in [(6, 'geom'), (8, 'easymode'), (9, 'hyllian')]:
    g = band_depths(f'ramp_{t}.png', (0.04, 0.62))
    check(f'{name}-style beams swell with brightness', g[0] > 2 * g[4] and g[4] >= 0.1,
          'gap depth 15%..100%: ' + ' '.join(f'{d:.2f}' for d in g))
for t, name in [(7, 'lottes'), (10, 'mame-sine')]:
    g = band_depths(f'ramp_{t}.png', (0.04, 0.62))
    check(f'{name}-style keeps a constant line profile', max(g) < 1.5 * min(g), 'gap depth 15%..100%: ' + ' '.join(f'{d:.2f}' for d in g))
for t in (6, 7, 8, 9, 10):
    c = band_depths(f'ramp_{t}.png', (0.68, 0.98))
    check(f'style {t}: lines stay visible in saturated colours', min(c) >= 0.1, 'R,G,B,Y,C: ' + ' '.join(f'{d:.2f}' for d in c))
# Lowered resolution: 60 rows on 1080p content -> 107x60 blocks over the picture rect.
import json
rep = next(e for e in json.load(open(out / 'effects.json')) if e.get('label') == 'px-hard')
pr = rep['pictureRect']; bw, bh = pr['w'] / 107.0, pr['h'] / 60.0
def intra_block_std(n):
    im = RGB(n); vals = []
    for by in range(4, 56, 3):
        for bx in range(4, 103, 3):
            x0 = int(pr['x'] + bx * bw) + 2; x1 = int(pr['x'] + (bx + 1) * bw) - 2
            y0 = int(pr['y'] + by * bh) + 2; y1 = int(pr['y'] + (by + 1) * bh) - 2
            if x1 > x0 and y1 > y0: vals.append(im[y0:y1, x0:x1].std(axis=(0, 1)).max())
    return float(np.mean(vals))
nat, hard, sharp, soft = (intra_block_std(n) for n in ('px_native.png', 'px_hard.png', 'px_sharp.png', 'px_soft.png'))
check('hard pixels: every block is uniform', hard < 0.5, f'mean in-block std {hard:.2f} (native {nat:.2f})')
check('sharp pixels: blocks uniform away from their edges', sharp < 0.5, f'mean in-block std {sharp:.2f}')
check('soft pixels: smooth, not blocky', soft > hard + 1.0, f'mean in-block std {soft:.2f}')
orig = Image.open(out / 'px_original.png')
check('original-frame screenshot keeps full resolution', orig.size == (1920, 1080), f'{orig.size}')
a = RGB('px_compare.png'); b = RGB('px_compare_native.png'); half = int(pr['x'] + pr['w'] * 0.5) - 3
d = np.abs(a[:, :half] - b[:, :half]).max()
check('compare view: original side unaffected by lowered resolution', d <= 2, f'max diff on the original side {d:.0f}')
print(f"\n{fails} effect check(s) failed" if fails else "\nAll effect checks passed")
sys.exit(1 if fails else 0)
