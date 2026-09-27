#!/usr/bin/env python3
"""Checks tests/automation/scene.txt (desk-mode scenes). Usage: check-scene.py OUT_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
def rgba(n): return np.asarray(Image.open(out / n).convert('RGBA')).astype(float)
def grain_period(n):
    im = rgba(n); lum = im[..., :3].mean(axis=2)
    sat = im[..., :3].max(axis=2) - im[..., :3].min(axis=2)
    ys, xs = np.nonzero(sat > 120); tv_h = ys.max() - ys.min()
    h, w = lum.shape
    reg = lum[int(h * 0.72):int(h * 0.97), int(w * 0.03):max(int(w * 0.03) + 10, xs.min() - 60)]
    lags = []
    for col in range(0, reg.shape[1], 7):
        p = reg[:, col] - reg[:, col].mean()
        if p.std() < 0.5: continue
        ac = np.correlate(p, p, 'full')[len(p) - 1:]; ac /= ac[0]
        dip = np.argmax(ac < 0.2) if (ac < 0.2).any() else 0
        if dip: lags.append(dip + int(np.argmax(ac[dip:dip + reg.shape[0] // 2])))
    return float(np.median(lags)) / tv_h
g = grain_period('scene_evening.png')
check('desk grain at a believable scale (1.9.2 measured 0.123)', g <= 0.08, f'texture period {g:.3f} x the picture height')
wall = lambda n: rgba(n)[250:420, 60:320, :3].mean()
e, ni, d = wall('scene_evening.png'), wall('scene_night.png'), wall('scene_dark.png')
check('moods: evening > night > lights off (wall)', e > ni > d, f'{e:.1f} > {ni:.1f} > {d:.1f}')
front = lambda n: rgba(n)[860:960, 820:1100, :3].reshape(-1, 3).mean(axis=0)
b, w, r = front('solid_black.png'), front('solid_white.png'), front('solid_red.png')
check('the picture lights the desk in front of the set', w.mean() - b.mean() > 30, f'white {w.mean():.1f} vs black {b.mean():.1f}')
check('in the picture\'s colour', r[0] > 3 * max(r[1], 1) and r[0] > 3 * max(r[2], 1), f'red picture: R {r[0]:.0f}, G {r[1]:.0f}, B {r[2]:.0f}')
def around(n):
    im = rgba(n)[..., :3]; sat = im.max(axis=2) - im.min(axis=2)
    ys, xs = np.nonzero(sat > 120); y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    ring = np.zeros(sat.shape, bool); ring[max(0, y0 - 120):y1 + 120, max(0, x0 - 160):x1 + 160] = True
    ring[y0 - 40:y1 + 60, x0 - 60:x1 + 60] = False                          # leave out the set itself
    return im[ring].mean()
fo, fn = around('scene_fog.png'), around('scene_night.png')
check('fog glows around the set', fo > fn * 1.5 + 3, f'surroundings {fn:.1f} -> {fo:.1f} with thick fog')
check('scenes are opaque (nothing shows through)', all((rgba(n)[..., 3] == 255).mean() > 0.999 for n in ('scene_evening.png', 'scene_fog.png')), '100%')
ref = rgba('scene_ref_full.png')[..., :3]; full = rgba('scene_full.png')[..., :3]
dmax = np.abs(ref - full).max() if ref.shape == full.shape else 999
check('flying in with a scene and thick fog lands on the exact fullscreen frame', dmax <= 2, f'max diff {dmax:.0f}')
rep = {e['label']: e for e in json.load(open(out / 'scene.json')) if e['cmd'] == 'report'}
a, bb = rep['scene-a'], rep['scene-b']
check('scene settings are remembered', (bb['deskScene'], bb['sceneMood'], bb['sceneFog']) == (a['deskScene'], a['sceneMood'], a['sceneFog']) == ('desk', 2, 2),
      f"{bb['deskScene']}, mood {bb['sceneMood']}, fog {bb['sceneFog']}")
print(f"\n{fails} scene check(s) failed" if fails else "\nAll scene checks passed")
sys.exit(1 if fails else 0)
