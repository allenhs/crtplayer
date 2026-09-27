#!/usr/bin/env python3
"""Checks tests/automation/theater.txt (movie theater, film looks). Usage: check-theater.py OUT_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
rep = {e['label']: e for e in json.load(open(out / 'theater.json')) if e['cmd'] == 'report'}
img = lambda n: np.asarray(Image.open(out / n).convert('RGB')).astype(float)
c, o = rep['t-closed'], rep['t-open']
check('closed before the film starts, house lights up', c['curtainOpen'] == 0 and c['houseLights'] == 1, f"curtain {c['curtainOpen']}, house {c['houseLights']}")
check('the theater shows its screen instead of the set', c['deskCabinetDrawn'] == 'theater-screen', c['deskCabinetDrawn'])
check('the film look is used in the theater', c['preset'] == 'Film Print (35mm)', c['preset'])
cl = img('th_closed.png'); h, w, _ = cl.shape
steps = [np.abs(np.diff(cl[int(h * 0.15):int(h * 0.62), int(w * f) - 3:int(w * f) + 3].mean(axis=(1, 2)))).max() for f in (0.12, 0.20, 0.80, 0.88)]
check('sconce light fades smoothly (no hard cut-off below the lamps)', max(steps) < 12, f'largest step {max(steps):.1f}')
centre = cl[int(h * 0.3):int(h * 0.55), int(w * 0.4):int(w * 0.6)].reshape(-1, 3).mean(axis=0)
check('closed curtains cover the screen (red velvet)', centre[0] > 2.2 * centre[1] and centre[0] > 2.2 * centre[2], f'centre {centre.round(0)}')
check('the curtains open while it plays, house lights down', o['curtainOpen'] == 1 and o['houseLights'] == 0, f"curtain {o['curtainOpen']}, house {o['houseLights']}")
op = img('th_open.png')
oc = op[int(h * 0.3):int(h * 0.55), int(w * 0.4):int(w * 0.6)]
colourful = float((oc.max(axis=2) - oc.min(axis=2)).mean())
check('open: the picture shows', colourful > 60, f'colourfulness at the centre {colourful:.0f}')
hz = img('th_haze.png')
above = lambda a: a[int(h * 0.03):int(h * 0.17), int(w * 0.3):int(w * 0.7)].mean()
check("haze: the projector's light shows in the air above the screen", above(hz) > above(op) + 15, f'{above(op):.1f} -> {above(hz):.1f}')
ROW0, ROWD, ROWS = 1.2, 0.22, 20
def eye(z): return -0.45 + 0.07 + min(max((z - ROW0) / ROWD, 0), ROWS - 1) * 0.07 + 0.23
for lab in ('t-back', 't-middle', 't-front'):
    c = rep[lab]['theaterCamera']
    check(f'{lab[2:]}: the camera sits in the rows, eye at seated height', ROW0 + 2.9 * ROWD <= c[2] <= ROW0 + (ROWS - 1) * ROWD and abs(c[1] - eye(c[2])) < 0.03,
          f'row {(c[2] - ROW0) / ROWD:.1f}, eye {c[1]:.2f} (seated {eye(c[2]):.2f})')
cb, cf = rep['t-back']['theaterCamera'], rep['t-front']['theaterCamera']
check('zoom moves between the back and the front rows', cb[2] - cf[2] > 2.5, f'rows {(cb[2] - ROW0) / ROWD:.1f} to {(cf[2] - ROW0) / ROWD:.1f}')
ct = rep['t-turned']['theaterCamera']
check('turning keeps you inside the auditorium', abs(ct[0]) < (2.39 + 0.30) / 2 + 0.9 - 0.2, f'x {ct[0]:.2f}')
seats = img('th_seats.png')[int(h * 0.72):, :]
red = (seats[..., 0] > seats[..., 1] * 1.6) & (seats[..., 0] > 12)
rows_var = float(seats[..., 0].std())
check('3D seats in the rows below the screen', red.mean() > 0.2 and rows_var > 3, f'{red.mean() * 100:.0f}% red fabric, variation {rows_var:.1f}')
an, ar = img('th_angle.png'), img('th_angle_ref.png')
seat_area = slice(int(h * 0.55), h)
dd = np.abs(an[seat_area] - ar[seat_area]).max(axis=2)
check('seats seen at an angle match the exact reference (no jagged edges)', (dd > 12).mean() < 0.002,
      f'{(dd > 12).mean() * 100:.2f}% of the seat area off (1.19% before the fix)')
m = rep['t-4x3']['maskRect']
check('masking fits a 4:3 film', abs((m[1] - m[0]) - 4 / 3) < 0.02 and abs((m[3] - m[2]) - 1) < 0.02, f'opening {m[1] - m[0]:.3f} x {m[3] - m[2]:.3f}')
ref, full = img('th_ref_full.png'), img('th_full.png')
d = np.abs(ref - full).max() if ref.shape == full.shape else 999
check('flying into the screen lands on the exact fullscreen frame', d <= 2, f'max diff {d:.0f}')
check('the curtains close at the end of the film', rep['t-end']['curtainOpen'] == 0, f"curtain {rep['t-end']['curtainOpen']}")
check('leaving desk mode brings your own look back', rep['t-left']['preset'] == 'Consumer Television', rep['t-left']['preset'])
a, b, cc = img('film_a.png'), img('film_b.png'), img('film_c.png')
check('film grain changes from one film frame to the next', np.abs(a - b).mean() > 2.0, f'mean difference {np.abs(a - b).mean():.2f}')
check('… and is the same when replayed at the same moment', np.abs(a - cc).max() <= 1, f'max difference {np.abs(a - cc).max():.0f}')
print(f"\n{fails} theater check(s) failed" if fails else "\nAll theater checks passed")
sys.exit(1 if fails else 0)
