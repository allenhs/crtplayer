#!/usr/bin/env python3
"""Checks the captures written by tests/automation/sim.txt (needs Pillow + numpy)."""
import json, sys, pathlib
import numpy as np
from PIL import Image

out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
def img(n): return np.asarray(Image.open(out / n).convert('RGB')).astype(int)
reps = {e['label']: e for e in json.load(open(out / 'sim.json')) if e['cmd'] == 'report'}
pr = reps['sim-base']['pictureRect']
y0, y1, x0, x1 = int(pr['y']) + 6, int(pr['y'] + pr['h']) - 6, int(pr['x']) + 6, int(pr['x'] + pr['w']) - 6
def colours(n):
    a = img(n)[y0:y1, x0:x1].reshape(-1, 3)
    return len(np.unique(a[:, 0] * 65536 + a[:, 1] * 256 + a[:, 2]))
full = colours('cd_0.png')
for n, name, limit in [('cd_3.png', '9-bit', 512), ('cd_6.png', '3-bit', 8), ('cd_7.png', 'Game Boy', 4), ('cd_8.png', 'CGA', 4), ('cd_9.png', 'EGA', 16)]:
    c = colours(n)
    check(f'{name} colour depth limits the palette', c <= limit and c < full, f'{c} distinct colours (limit {limit}; full colour {full})')
gb = img('cd_7.png')[y0:y1, x0:x1]
check('Game Boy palette is green-tinted', (gb[..., 1] >= gb[..., 0]).mean() > 0.99 and (gb[..., 1] >= gb[..., 2]).mean() > 0.99, 'G >= R and G >= B everywhere')
def block_err(n):
    ref = img('cd_0.png')[y0:y1, x0:x1].mean(axis=2); a = img(n)[y0:y1, x0:x1].mean(axis=2)
    h, w = (ref.shape[0] // 8) * 8, (ref.shape[1] // 8) * 8
    r = ref[:h, :w].reshape(h // 8, 8, w // 8, 8).mean(axis=(1, 3)); b = a[:h, :w].reshape(h // 8, 8, w // 8, 8).mean(axis=(1, 3))
    return float(np.abs(r - b).mean())
e0, e1 = block_err('cd_6.png'), block_err('cd_6_dither.png')
check('ordered dither reproduces tones better at a distance', e1 < e0 * 0.8, f'8x8-average error {e1:.2f} with dither vs {e0:.2f} without')
d_ntsc = np.abs(img('ntsc_f1.png') - img('ntsc_f2.png')).mean()
d_pal = np.abs(img('pal_f1.png') - img('pal_f2.png')).mean()
check('PAL fields run at 50 Hz (NTSC at 59.94 Hz)', d_ntsc > 2.0 and d_pal < 0.1,
      f'10.000 s vs 10.017 s: NTSC fields differ ({d_ntsc:.1f}), PAL same field ({d_pal:.2f})')
def line_alternation(n):
    a = img(n)[y0:y1, x0:x1].astype(float)
    ch = a - a.mean(axis=2, keepdims=True)                 # chroma-ish
    rows = ch.mean(axis=1)                                 # per-row mean chroma vector
    return float(np.abs(rows[1:] - rows[:-1]).mean())
hn, hp = line_alternation('hanover_ntsc.png'), line_alternation('hanover_pal.png')
check('PAL shows line-alternating hue (Hanover bars)', hp > hn * 1.3, f'row-to-row chroma change PAL {hp:.2f} vs NTSC {hn:.2f}')
# The window capture also contains Qt overlays (the toast message at the top centre), which
# are not part of the video frame: compare below them.
TOP = 90
live = img('pers1_b.png'); now = img('pers1_b_now.png')
live = live[TOP:now.shape[0], :now.shape[1]]          # the video area is the window's top-left
now = now[TOP:]
pl, pn = live.sum(axis=2), now.sum(axis=2)
brighter = (pl - pn > 24).mean(); darker = (pn - pl > 24).mean()
check('persistence leaves an afterglow of the previous frame (only ever brighter)', brighter > 0.002 and darker < 0.0005,
      f'{brighter * 100:.2f}% of pixels brighter than the same moment without afterglow, {darker * 100:.3f}% darker')
n0 = img('pers0_b_now.png'); q0 = img('pers0_b.png')[TOP:n0.shape[0], :n0.shape[1]]; n0 = n0[TOP:]
d0 = float(np.abs(q0.sum(axis=2) - n0.sum(axis=2)).max())
check('no afterglow with persistence off (live picture equals the screenshot)', d0 <= 6, f'max difference {d0:.0f}')
def lit_rows(n):
    a = img(n)[:, x0:x1].max(axis=2); return float((a.max(axis=1) > 40).mean())
def lit_cols(n):
    a = img(n)[y0:y1, :].max(axis=2); return float((a.max(axis=0) > 40).mean())
on20, ondone = lit_rows('on_020.png'), lit_rows('on_done.png')
check('power-on opens the raster from a line', on20 < ondone * 0.6, f'lit rows {on20 * 100:.0f}% at 20% vs {ondone * 100:.0f}% when done')
o11, o44, odone = lit_rows('off_011.png'), lit_cols('off_044.png'), img('off_done.png').max()
check('power-off collapses to a line, then a dot, then dark', o11 < ondone * 0.9 and o44 < 0.7 and odone < 20,
      f'lit rows {o11 * 100:.0f}% at 11%, lit columns {o44 * 100:.0f}% at 44%, max level {odone} at the end')
check('report shows the set as off after power-off', reps['sim-off']['moment'] == 'off', reps['sim-off']['moment'])
st = img('static.png')[y0:y1, x0:x1].mean(axis=2)
hf = float(np.abs(np.diff(st, axis=1)).mean()); base_hf = float(np.abs(np.diff(img('cd_0.png')[y0:y1, x0:x1].mean(axis=2), axis=1)).mean())
check('static replaces the picture with snow', hf > base_hf * 4 and reps['sim-static']['moment'] == 'static',
      f'pixel-to-pixel change {hf:.1f} vs picture {base_hf:.1f}')
a, b = img('osd_off.png'), img('osd_on.png')
diff = np.abs(a - b).sum(axis=2) > 30
hh, ww = diff.shape
corner = diff[int(pr['y']):int(pr['y'] + pr['h'] * 0.25), int(pr['x']):int(pr['x'] + pr['w'] * 0.5)].sum()
check('VCR text appears in the picture\'s top-left corner', diff.sum() > 500 and corner / max(1, diff.sum()) > 0.95,
      f'{diff.sum()} changed pixels, {corner / max(1, diff.sum()) * 100:.1f}% in the top-left quarter')
print(f"\n{fails} simulation check(s) failed" if fails else "\nAll simulation checks passed")
sys.exit(1 if fails else 0)
