#!/usr/bin/env python3
"""Checks tests/automation/cg.txt (the 90s CG room). Usage: check-cg.py OUT_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
img = lambda n: np.asarray(Image.open(out / n).convert('RGB')).astype(float)
rep = {e['label']: e for e in json.load(open(out / 'cg.json')) if e['cmd'] == 'report'}
ref, full = img('cg_ref_full.png'), img('cg_full.png')
d = np.abs(ref - full).max() if ref.shape == full.shape else 999
check('the sweeping fly-in lands on the exact fullscreen frame', d <= 2, f'max diff {d:.0f}')
ws, su, sp = img('cg_ws.png'), img('cg_sunset.png'), img('cg_space.png')
h, w, _ = ws.shape
sky = lambda a: a[:int(h * 0.12)].reshape(-1, 3).mean(axis=0)
a, b, c = sky(ws), sky(su), sky(sp)
check('palettes: teal workstation, orange sunset, dark space', a[2] > a[0] + 20 and b[0] > b[2] + 40 and c.mean() < a.mean() * 0.4,
      f'workstation {a.round(0)}, sunset {b.round(0)}, space {c.round(0)}')
floor = lambda x: x[int(h * 0.8):]
check('the neon grid floor differs from the checkerboard', np.abs(floor(img('cg_grid.png')) - floor(ws)).mean() > 25,
      f'mean difference {np.abs(floor(img("cg_grid.png")) - floor(ws)).mean():.1f}')
no = img('cg_noobj.png')
changed = (np.abs(no - ws).max(axis=2) > 40).mean()   # the objects are a small part of the view
check('the objects can be turned off', changed > 0.02, f'{changed * 100:.1f}% of the view changes')
pl = img('cg_plinth.png')
check('the plinth differs from the pedestal', np.abs(pl - ws).mean() > 2, f'mean difference {np.abs(pl - ws).mean():.1f}')
ns = img('cg_noshadow.png')
check('hard shadows (quality medium vs low)', np.abs(floor(ns) - floor(ws)).mean() > 1.0,
      f'floor difference {np.abs(floor(ns) - floor(ws)).mean():.2f}')
low, rnd = rep['cg-low'], rep['cg-round']
check('the camera stays above the floor', low['deskClearance'] > 0.1, f"{low['deskClearance']:.2f} above it (asked for pitch -40)")
check('an open world: the camera goes all the way round', abs(rnd['deskYaw'] - 170) < 0.5, f"yaw {rnd['deskYaw']:.1f}")
turn = lambda a, b: (rep[b]['deskYaw'] - rep[a]['deskYaw'] + 540) % 360 - 180
brief, seeking, idle, playing = turn('orbit-a', 'orbit-b'), turn('orbit-b', 'orbit-c'), turn('orbit-c', 'orbit-d'), turn('play-a', 'play-b')
# (real time runs ahead of the script's waits under slow rendering, so the idle time the
# player itself measured is what shows these moments were under 10 s)
ib, ic = rep['orbit-b']['cgIdleMs'], rep['orbit-c']['cgIdleMs']
sought = rep['orbit-c']['seeksNoted'] - rep['orbit-b']['seeksNoted']
check('demo-reel orbit: not while briefly paused, seeking, or playing',
      abs(brief) < 0.5 and abs(seeking) < 0.5 and abs(playing) < 0.5 and ib < 10000 and ic < 10000 and sought >= 10,
      f'paused ({ib / 1000:.1f} s idle) {brief:+.1f} deg; scrubbing while paused ({sought} seeks, then {ic / 1000:.1f} s idle) '
      f'{seeking:+.1f} deg; playing {playing:+.1f} deg')
check('demo-reel orbit once stopped and untouched for 10 s', idle > 3, f'{idle:+.1f} deg over the next 10.5 s')
red, white = img('cg_red.png'), img('cg_white.png')
# outside the set's own screen: where the red picture turns the scene red (reflections, glow)
dr = (red[..., 0] - red[..., 1]) - (white[..., 0] - white[..., 1])
scr = (red[..., 0] > 200) & (red[..., 1] < 60) & (white.mean(axis=2) > 200)   # the screen itself
reddened = (dr > 25) & ~scr
check('the picture is reflected in the scene', reddened.sum() > 800, f'{int(reddened.sum())} pixels outside the screen turn red')
rs, rd_ = rep['reveal-start'], rep['reveal-done']
check('tile reveal: starts when playback starts, and completes', rs['cgReveal'] < 0.8 and rd_['cgReveal'] == 1,
      f"{rs['cgReveal']:.2f} just after starting, then {rd_['cgReveal']:.0f}")
rv = {k: rep[k]['cgReveal'] for k in ('seek-fwd', 'seek-back', 'scrubbed', 'short-pause', 'long-pause')}
check('no tile reveal for skipping forward or back, scrubbing, or a short pause',
      all(rv[k] == 1 for k in ('seek-fwd', 'seek-back', 'scrubbed', 'short-pause')),
      ', '.join(f'{k} {v:.2f}' for k, v in rv.items() if k != 'long-pause'))
check('the tile reveal again after a long pause (10 s or more)', rv['long-pause'] < 0.8, f"{rv['long-pause']:.2f} just after resuming")
# object sets
s0 = img('cg_set0.png')
diffs = [(np.abs(img(f'cg_set{k}.png') - s0).max(axis=2) > 40).mean() for k in (1, 2, 3, 4)]
check('each object set changes the scene (toybox, organic, mannequins, mixed)', min(diffs) > 0.01, ', '.join(f'{d * 100:.1f}%' for d in diffs))
# the background crowd, and walkers that actually walk
crowd = (np.abs(img('cg_bgon.png') - img('cg_bgoff.png')).max(axis=2) > 40).mean()
check('the background crowd appears with the default set too', crowd > 0.005, f'{crowd * 100:.1f}% of the view changes')
wa, wb, wc = img('cg_walk_a.png'), img('cg_walk_b.png'), img('cg_walk_c.png')
moved = (np.abs(wa - wb).max(axis=2) > 40).mean()
check('the mannequins walk around (0.7 s later they have moved), deterministically', moved > 0.002 and np.abs(wa - wc).max() <= 1,
      f'{moved * 100:.2f}% of the view changes; same moment again: max diff {np.abs(wa - wc).max():.0f}')
# stars: small round points, not squares
st = img('cg_stars.png'); sky_ = st[:int(st.shape[0] * 0.3)].mean(axis=2)
bright = sky_ > max(60.0, np.percentile(sky_, 99.8))
from collections import deque
seen = np.zeros_like(bright); sizes, aspects = [], []
for y, x in zip(*np.nonzero(bright)):
    if seen[y, x]: continue
    q = deque([(y, x)]); seen[y, x] = True; pts = []
    while q:
        cy, cx = q.popleft(); pts.append((cy, cx))
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = cy + dy, cx + dx
            if 0 <= ny < bright.shape[0] and 0 <= nx < bright.shape[1] and bright[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True; q.append((ny, nx))
    ys, xs = zip(*pts); sizes.append(len(pts)); aspects.append((max(xs) - min(xs) + 1) / (max(ys) - min(ys) + 1))
check('stars are small round points (not squares)', len(sizes) >= 10 and max(sizes) <= 40 and 0.5 <= float(np.median(aspects)) <= 2.0,
      f'{len(sizes)} stars, up to {max(sizes) if sizes else 0} px, median width/height {float(np.median(aspects)) if aspects else 0:.2f}')
# your 3D models
m = rep['models']
check('your models load (OBJ, binary STL, text STL); a broken file is skipped', m['modelsShown'] == 3 and any('d_broken.obj' in x for x in m['modelsSkipped']),
      f"{m['modelsShown']} shown; skipped: {'; '.join(m['modelsSkipped'])}")
nm, wm, br = img('cg_nomodels.png'), img('cg_models.png'), img('cg_bronze.png')
appear = (np.abs(wm - nm).max(axis=2) > 40).mean()
check('they appear in the scene', appear > 0.01, f'{appear * 100:.1f}% of the view changes')
lower = slice(int(h * 0.55), h)
darker = ((nm[lower].mean(axis=2) - wm[lower].mean(axis=2)) > 12).sum()
brighter = ((wm[lower].mean(axis=2) - nm[lower].mean(axis=2)) > 12).sum()
check('they cast shadows on the floor, and show in the mirror floor', darker > 500 and brighter > 500, f'{darker} floor pixels darker, {brighter} brighter')
warm = lambda a: ((a[..., 0] > a[..., 2] * 1.4) & (a[..., 0] > 60)).sum()
check('the bronze finish differs from marble', warm(br) > warm(wm) + 500, f'warm pixels {warm(wm)} (marble) -> {warm(br)} (bronze)')
alpha = np.asarray(Image.open(out / 'cg_models.png').convert('RGBA'))[..., 3]
check('the scene stays opaque (the floor mask is reset)', (alpha == 255).mean() > 0.999, f'{(alpha == 255).mean() * 100:.2f}% opaque')
check('your models folder is remembered', rep['models-again']['modelsShown'] == 3, f"{rep['models-again']['modelsShown']} shown after leaving and re-entering desk mode")
print(f"\n{fails} CG room check(s) failed" if fails else "\nAll CG room checks passed")
sys.exit(1 if fails else 0)
