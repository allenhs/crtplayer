#!/usr/bin/env python3
"""Checks tests/automation/wall.txt (camera floor limit, wall-mounted TV scene, pictures).
Usage: check-wall.py OUT_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
rep = {e['label']: e for e in json.load(open(out / 'wall.json')) if e['cmd'] == 'report'}
fd, ft = rep['floor-desk'], rep['floor-desktop']
check('in a scene the camera stays above the floor', fd['deskPitch'] > -30 and fd['deskClearance'] > 0.1,
      f"asked for pitch -30: got {fd['deskPitch']:.1f}, {fd['deskClearance']:.2f} above the floor")
check('on the transparent desktop it may still look from below', abs(ft['deskPitch'] + 30) < 0.01, f"pitch {ft['deskPitch']:.1f}")
check('frames per layout (one each side / two each side)', rep['wall-1']['framesShown'] == 2 and rep['wall-4']['framesShown'] == 4,
      f"{rep['wall-1']['framesShown']} and {rep['wall-4']['framesShown']}")
check('a missing picture file is skipped', rep['wall-missing']['picturesLoaded'] == 3 and rep['wall-missing']['framesShown'] == 3,
      f"{rep['wall-missing']['picturesLoaded']} loaded, {rep['wall-missing']['framesShown']} frames")
check('pictures are remembered', rep['wall-reenter']['picturesLoaded'] == 4 and rep['wall-reenter']['deskScene'] == 'wall',
      f"{rep['wall-reenter']['deskScene']}, {rep['wall-reenter']['picturesLoaded']} pictures")
def img(n): return np.asarray(Image.open(out / n).convert('RGBA')).astype(float)
def screen_box(a):
    sat = a[..., :3].max(axis=2) - a[..., :3].min(axis=2)
    lum = a[..., :3].mean(axis=2)
    ys, xs = np.nonzero((sat > 150) & (lum > 80))          # the brightly lit colour bars
    return int(np.percentile(xs, 2)) - 25, int(np.percentile(xs, 98)) + 25
# Pictures on the wall are lit by the room (darker, warmer than the files), so they are
# found by hue, not by exact colour; the set's own screen area is left out.
HUES = {
    'orange': lambda R, G, B: (R > 1.6 * G) & (G > 1.2 * B) & (R > 40),
    'blue': lambda R, G, B: (B > 1.4 * R) & (B > 1.1 * G) & (B > 30),
    'green': lambda R, G, B: (G > 1.4 * R) & (G > 1.3 * B) & (G > 30),
    'magenta': lambda R, G, B: (R > 1.5 * G) & (B > 1.5 * G) & (R > 40),
}
def hue_sides(a, hue):
    x0, x1 = screen_box(a)
    m = HUES[hue](a[..., 0], a[..., 1], a[..., 2])
    return int(m[:, :x0].sum()), int(m[:, x1:].sum()), m, x0
b = img('wall_black.png')
sl, sr, _, _ = hue_sides(b, 'orange')
ml, mr, _, _ = hue_sides(b, 'blue')
check('the first picture hangs on the left', sl > 400 and sl > 20 * max(sr, 1), f'sunset pixels left {sl}, right {sr}')
check('the second picture hangs on the right', mr > 400 and mr > 5 * max(ml, 1), f'mountain pixels right {mr}, left {ml}')
f4 = img('wall_four.png')
g = hue_sides(f4, 'green')[1]; mg = hue_sides(f4, 'magenta')[1]
check('with four, the third and fourth are on the right', g > 150 and mg > 150, f'green {g}, magenta {mg} pixels right of the set')
def ring(a):   # the moulding at the left side of the sunset picture (its picture light is above it)
    _, _, m, x0 = hue_sides(a, 'orange')
    m[:, x0:] = False
    ys, xs = np.nonzero(m)
    xl = int(np.percentile(xs, 1))
    ya, yb = int(np.percentile(ys, 30)), int(np.percentile(ys, 70))
    return a[ya:yb, max(0, xl - 22):xl - 8, :3].reshape(-1, 3).mean(axis=0)
rb, rg = ring(b), ring(img('wall_gold.png'))
check('gold frames look gold, black frames dark', rg[0] > rb[0] + 25 and rg[0] > rg[2] * 1.5, f'black {rb.round(0)}, gold {rg.round(0)}')
def sunset(a):   # the left picture's orange area: centre and size
    _, _, m, x0 = hue_sides(a, 'orange')
    m[:, x0:] = False
    ys, xs = np.nonzero(m)
    return xs.mean(), ys.mean(), len(xs)
bx, by, bn = sunset(b)
px_, py_, pn = sunset(img('wall_placed.png'))
check('placement: pictures higher, further from the set, larger', py_ < by - 10 and px_ < bx - 10 and pn > bn * 1.3,
      f'centre ({bx:.0f},{by:.0f}) -> ({px_:.0f},{py_:.0f}), area {bn} -> {pn}')
HALF_W, CEIL, DROP = 3.8, 5.0, 1.7
ct, chh = rep['room-turn']['theaterCamera'], rep['room-high']['theaterCamera']
check('turned hard to the side, the camera stays inside the side walls', abs(ct[0]) <= HALF_W - 0.29, f'x {ct[0]:.2f} (walls at +-{HALF_W})')
check('from high above, the camera stays under the ceiling', chh[1] + DROP <= CEIL - 0.29, f'{chh[1] + DROP:.2f} above the floor (ceiling {CEIL})')
side = img('room_side.png')
edges = min(side[:, :int(side.shape[1] * 0.08), :3].mean(), side[:, -int(side.shape[1] * 0.08):, :3].mean())
check('side walls: no black void at the edges of the view', edges > 2.5, f'darkest edge strip {edges:.1f}')
up = img('room_up.png')
check('looking up shows the ceiling', up[:int(up.shape[0] * 0.08), :, :3].mean() > 2.5, f'top strip {up[:int(up.shape[0] * 0.08), :, :3].mean():.1f}')
check('the wall scene is opaque', (b[..., 3] == 255).mean() > 0.999, '100%')
ref = img('wall_ref_full.png')[..., :3]; full = img('wall_full.png')[..., :3]
d = np.abs(ref - full).max() if ref.shape == full.shape else 999
check('flying in from the wall scene lands on the exact fullscreen frame', d <= 2, f'max diff {d:.0f}')
pv = [img(f'preview_{i}.png')[..., :3] for i in range(3)]
diff = min(np.abs(pv[i] - pv[j]).mean() for i in range(3) for j in range(i + 1, 3))
check('scene previews render, and differ', all(p.shape[:2] == (216, 384) for p in pv) and diff > 5, f'smallest difference {diff:.1f}')
print(f"\n{fails} wall check(s) failed" if fails else "\nAll wall checks passed")
sys.exit(1 if fails else 0)
