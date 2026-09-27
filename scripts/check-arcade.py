#!/usr/bin/env python3
"""Checks tests/automation/arcade.txt (the arcade cabinet). Usage: check-arcade.py OUT_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
log = json.load(open(out / 'arcade.json'))
rep = {e['label']: e for e in log if e['cmd'] == 'report'}
titles = [(e.get('input', ''), e.get('result')) for e in log if e['cmd'].split(' ')[0] == 'marqueetitle']
want = {'Oblivion.2013.1080p.BluRay.x264': 'OBLIVION', 'The_Matrix_(1999)_720p': 'THE MATRIX', 'Some.Show.S01E02.HDTV.XviD': 'SOME SHOW',
        'sd_4x3_h264': 'SD 4X3', 'Street Fighter II': 'STREET FIGHTER II', '': 'CRT PLAYER'}
bad = [(r, got) for r, got in titles if want.get(r) != got]
check('marquee titles: file names tidied', not bad and len(titles) == 6, '; '.join(f'{r!r} -> {g!r}' for r, g in titles) if not bad else f'wrong: {bad}')
img = lambda n: np.asarray(Image.open(out / n).convert('RGB')).astype(float)
f = rep['front']
check('the arcade cabinet is drawn, with the title on its marquee', f['deskCabinetDrawn'] == 'arcade' and f['marqueeText'] == 'SD 4X3',
      f"{f['deskCabinetDrawn']}, {f['marqueeText']!r}")
ref, full = img('arc_ref_full.png'), img('arc_full.png')
d = np.abs(ref - full).max() if ref.shape == full.shape else 999
check('the fly-in lands on the exact fullscreen frame', d <= 2, f'max diff {d:.0f}')
t = f['deskTvRect']
check('an upright cabinet: much taller than wide', t['h'] > t['w'] * 1.25, f"{t['w']:.0f} x {t['h']:.0f}")
g = f['deskGlassRect']
check('the screen sits in its upper half', g['y'] + g['h'] / 2 < t['y'] + t['h'] * 0.5, f"screen centre {g['y'] + g['h'] / 2:.0f}, cabinet {t['y']:.0f}..{t['y'] + t['h']:.0f}")
arts = [img(f'art{k}.png') for k in range(4)]
diffs = [(np.abs(arts[i] - arts[j]).max(axis=2) > 40).mean() for i in range(4) for j in range(i + 1, 4)]
check('four different art styles', min(diffs) > 0.01, f'least different pair: {min(diffs) * 100:.1f}% of the view')
for lab, sc in (('in-wall', 'wall'), ('in-cg', 'cg'), ('in-desk', 'desk')):
    r = rep[lab]
    check(f'{sc} scene: it stands on the floor', r['deskScene'] == sc and abs(r['floorDrop']) < 1e-6 and r['deskCabinetDrawn'] == 'arcade',
          f"floor {r['floorDrop']:.2f} below it, drawn {r['deskCabinetDrawn']}")
check('the theater still shows its own screen', rep['in-theater']['deskCabinetDrawn'] == 'theater-screen', rep['in-theater']['deskCabinetDrawn'])
p = rep['portrait']
pt, pg = p['deskTvRect'], p['deskGlassRect']
check('a vertical game: the screen turns, the cabinet stays upright',
      p['deskPivot'] and pg['h'] > pg['w'] and pt['h'] > pt['w'] * 1.25, f"screen {pg['w']:.0f} x {pg['h']:.0f}, cabinet {pt['w']:.0f} x {pt['h']:.0f}")
ma, mb = img('marquee_a.png'), img('marquee_b.png')
alpha = np.asarray(Image.open(out / 'marquee_a.png').convert('RGBA'))[..., 3]
check('the marquee lettering follows the video', (alpha > 128).mean() > 0.05 and np.abs(ma - mb).mean() > 3 and p['marqueeText'] == 'VERTICAL 9X16',
      f"{(alpha > 128).mean() * 100:.0f}% lettered; next video: {p['marqueeText']!r}")
ag = rep['again']
check('the arcade cabinet and its art are remembered', ag['deskCabinet'] == 'arcade' and ag['arcadeArt'] == 1, f"{ag['deskCabinet']}, art {ag['arcadeArt']}")
print(f"\n{fails} arcade check(s) failed" if fails else "\nAll arcade checks passed")
sys.exit(1 if fails else 0)
