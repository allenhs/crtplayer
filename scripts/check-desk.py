#!/usr/bin/env python3
"""Checks the captures written by tests/automation/desk.txt (needs Pillow + numpy)."""
import sys, pathlib
import numpy as np
from PIL import Image

out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
def rgba(n): return np.asarray(Image.open(out / n).convert('RGBA'))

ref = np.asarray(Image.open(out / 'desk_ref_fullscreen.png').convert('RGB'), float)
full = rgba('desk_full.png')
same_size = ref.shape[:2] == full.shape[:2]
d = np.abs(ref - full[..., :3].astype(float)) if same_size else None
check('landing frame equals the regular fullscreen frame', same_size and d.max() <= 2,
      f'max diff {d.max():.0f}, mean {d.mean():.3f}' if same_size else f'size {ref.shape} vs {full.shape}')
check('fullscreen phase is fully opaque', (full[..., 3] == 255).all(), f'{(full[..., 3] == 255).mean() * 100:.1f}% opaque')

idle = rgba('desk_idle.png'); A = idle[..., 3]
check('desk: transparent around the set', (A == 0).mean() > 0.5 and (A == 255).mean() > 0.05,
      f'{(A == 0).mean() * 100:.1f}% transparent, {(A == 255).mean() * 100:.1f}% opaque, {((A > 0) & (A < 255)).mean() * 100:.1f}% soft shadow')
def bbox(img):
    ys, xs = np.nonzero(img[..., 3] == 255); return xs.min(), xs.max(), ys.min(), ys.max()
b0, b1 = bbox(idle), bbox(rgba('desk_back.png'))
check('set returns to exactly where it was', b0 == b1, f'{b0} -> {b1}')
fly = rgba('desk_flying.png')
check('flight in progress differs from both ends', not np.array_equal(fly, idle) and not np.array_equal(fly, full), 'intermediate frame captured')
def glass_aspect(n):
    x0, x1, y0, y1 = bbox(rgba(n)); return (x1 - x0) / max(1, (y1 - y0))
v, w, c = glass_aspect('desk_vertical.png'), glass_aspect('desk_wide.png'), glass_aspect('desk_wide_classic.png')
check('portrait video gives a tall (pivoted) set', v < 1.0, f'cabinet w/h {v:.2f}')
check('16:9 follow-video set is wider than the classic 4:3 set', w > c * 1.15, f'{w:.2f} vs {c:.2f}')
pf = rgba('desk_panel_full.png')
dp = np.abs(ref - pf[..., :3].astype(float)) if ref.shape[:2] == pf.shape[:2] else None
check('flat panel: landing frame equals the regular fullscreen frame', dp is not None and dp.max() <= 2,
      f'max diff {dp.max():.0f}' if dp is not None else 'size mismatch')
def opaque_bbox_area(n):
    x0, x1, y0, y1 = bbox(rgba(n)); return (x1 - x0) * (y1 - y0)
pa, ca = (rgba('desk_panel.png')[..., 3] == 255).mean(), (idle[..., 3] == 255).mean()
check('flat panel is a slimmer set than the CRT (less opaque area at the same angle)', pa < ca * 0.95,
      f'opaque {pa * 100:.1f}% vs CRT {ca * 100:.1f}%')
fc = rgba('desk_flatcrt.png')[..., :3].astype(float); cr = idle[..., :3].astype(float)
check('flat-face CRT differs from the curved-glass CRT (flat glass)', np.abs(fc - cr).mean() > 0.5,
      f'mean difference {np.abs(fc - cr).mean():.2f}')
bf = rgba('desk_beige_full.png')
db = np.abs(ref - bf[..., :3].astype(float)) if ref.shape[:2] == bf.shape[:2] else None
check('beige monitor: landing frame equals the regular fullscreen frame', db is not None and db.max() <= 2,
      f'max diff {db.max():.0f}' if db is not None else 'size mismatch')
sets = {'crt': idle, 'flat-crt': rgba('desk_flatcrt.png'), 'flat-panel': rgba('desk_panel.png'),
        'wood': rgba('desk_wood.png'), 'pvm': rgba('desk_pvm.png'), 'beige': rgba('desk_beige.png')}
names = list(sets); worst = (1e9, '')
for i in range(len(names)):
    for j in range(i + 1, len(names)):
        dd = float(np.abs(sets[names[i]][..., :3].astype(float) - sets[names[j]][..., :3].astype(float)).mean())
        if dd < worst[0]: worst = (dd, f'{names[i]}/{names[j]}')
check('all six sets look different from each other', worst[0] > 0.5, f'smallest mean difference {worst[0]:.2f} ({worst[1]})')
check('every set is transparent around itself', all((a[..., 3] == 0).mean() > 0.4 for a in sets.values()),
      ', '.join(f'{k} {(a[..., 3] == 0).mean() * 100:.0f}%' for k, a in sets.items()))
def gap_columns(n):
    # Columns where the set's opaque pixels form more than one run: something floats.
    a = rgba(n)[..., 3] == 255
    count = 0
    for x in range(a.shape[1]):
        col = a[:, x].astype(int)
        runs = int((np.diff(np.concatenate([[0], col, [0]])) == 1).sum())
        if runs > 1: count += 1
    return count
g = gap_columns('desk_wood_low.png')
check('wood console legs meet the cabinet (no floating legs)', g == 0, f'{g} pixel columns with a gap')
def shadow_hole(n):
    # Shadow opacity going down the screen below the cabinet's bottom edge, averaged over
    # columns between the legs. From this angle that runs along the floor from behind the
    # set, under it, to in front of it. A correct contact shadow has no gap under the set;
    # the 1.7.1 bug left a hole there (the shadow looked like an outline).
    a = rgba(n)[..., 3].astype(float)
    opaque = a == 255
    lowest = np.array([np.nonzero(opaque[:, x])[0].max() if opaque[:, x].any() else -1 for x in range(a.shape[1])])
    cols = np.nonzero(lowest >= 0)[0]
    body = np.median(lowest[cols])
    mid = [x for x in cols if abs(lowest[x] - body) < 6]
    mid = mid[len(mid) // 4: 3 * len(mid) // 4]
    prof = np.mean([a[lowest[x] + 1:lowest[x] + 150, x] for x in mid], axis=0) / 255
    dark = np.nonzero(prof >= 0.3)[0]
    if len(dark) < 2: return 0.0, prof.max()
    span = prof[dark[0]:dark[-1] + 1]
    inner = span[len(span) // 5: len(span) - len(span) // 5]      # the middle, away from the edges
    return float(inner.min()), float(prof.max())
hole, peak = shadow_hole('desk_wood_front.png')
check('shadow is solid under a set on legs (no hole under the middle)', hole >= 0.3,
      f'lowest opacity under the set {hole:.2f} (darkest {peak:.2f})')
def shadow_fraction(n):
    a = rgba(n)[..., 3]
    return float(((a > 0) & (a < 255)).mean())   # the only semi-transparent pixels are the soft shadow
below, above = shadow_fraction('desk_below.png'), shadow_fraction('desk_above.png')
check('viewed from below the floor plane, the shadow disappears', below < 0.0005 and above > 0.01,
      f'shadow pixels: {below * 100:.3f}% from below vs {above * 100:.1f}% from above')
import json
reps = {e['label']: e for e in json.load(open(out / 'desk.json')) if e['cmd'] == 'report'}
sb, sa = reps['desk-below']['deskShadowVisible'], reps['desk-above']['deskShadowVisible']
check('shadow (and its click area) is off from below, on from above', sb is False and sa is True,
      f'shadow visible from below: {sb}, from above: {sa}')
print(f"\n{fails} desk check(s) failed" if fails else "\nAll desk checks passed")
sys.exit(1 if fails else 0)
