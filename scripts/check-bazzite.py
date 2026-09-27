#!/usr/bin/env python3
"""Checks the gamepad / room-backdrop and Game Mode runs. Usage: check-bazzite.py OUT_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image

out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
r = {e['label']: e for e in json.load(open(out / 'gamepad.json')) if e['cmd'] == 'report'}
pos = lambda k: r[k]['positionMs']
check('controller detected through SDL', '1 controller' in r['gp-attached']['gamepad'], r['gp-attached']['gamepad'])
check('A pauses and resumes', r['gp-a1']['state'] == 'paused' and r['gp-a2']['state'] == 'playing', f"{r['gp-a1']['state']} -> {r['gp-a2']['state']}")
check('D-pad right seeks +10 s', 14000 <= pos('gp-right') <= 17500, f"{pos('gp-right'):.0f} ms (from ~5 s)")
check('holding the D-pad repeats', pos('gp-hold') - pos('gp-right') >= 30000, f"+{(pos('gp-hold') - pos('gp-right')) / 1000:.0f} s in a 1.5 s hold")
check('RB: next preset', r['gp-rb']['preset'] != r['gp-hold']['preset'], f"{r['gp-hold']['preset']} -> {r['gp-rb']['preset']}")
check('D-pad up: volume +5 %', abs(r['gp-up']['volume'] - r['gp-rb']['volume'] - 0.05) < 0.011, f"{r['gp-rb']['volume']:.2f} -> {r['gp-up']['volume']:.2f}")
check('View: desk mode on and off', r['gp-desk-on']['deskMode'] and not r['gp-desk-off']['deskMode'], 'on, then off')
dy = r['gp-stick']['deskYaw'] - r['gp-desk-on']['deskYaw']
check('left stick turns the set (time-based)', dy >= 30, f'yaw +{dy:.0f} degrees in 1 s at full tilt')
check('Y flies in, B flies back out', r['gp-flown-in']['deskPhase'] == 'full' and r['gp-flown-out']['deskPhase'] == 'desk',
      f"{r['gp-flown-in']['deskPhase']}, then {r['gp-flown-out']['deskPhase']}")
rep = (out / 'gp-sysreport.txt').read_text()
check('system report lists the controller', 'Controllers: SDL' in rep, next((l for l in rep.splitlines() if l.startswith('Controllers')), 'missing'))
desk = np.asarray(Image.open(out / 'room_desk.png'))[..., 3]
check('room backdrop: nothing shows through', (desk == 255).mean() > 0.999, f'{(desk == 255).mean() * 100:.1f}% opaque')
ref = np.asarray(Image.open(out / 'room_ref.png').convert('RGB')).astype(int)
full = np.asarray(Image.open(out / 'room_full.png').convert('RGB')).astype(int)
d = np.abs(ref - full).max() if ref.shape == full.shape else 999
check('room backdrop: flying in still lands on the exact fullscreen frame', d <= 2, f'max diff {d}')
g = {e['label']: e for e in json.load(open(out / 'gamescope.json')) if e['cmd'] == 'report'}
check('Game Mode (gamescope) is detected', g['gs-start']['inGamescope'], str(g['gs-start']['inGamescope']))
check('Game Mode starts fullscreen', g['gs-start']['fullscreen'], str(g['gs-start']['fullscreen']))
check('Game Mode desk mode uses the room backdrop', g['gs-desk'].get('deskBackdrop') == 'room', g['gs-desk'].get('deskBackdrop'))
check('system report notes gamescope', 'gamescope' in (out / 'gs-sysreport.txt').read_text(), 'Session line')
print(f"\n{fails} Bazzite check(s) failed" if fails else "\nAll Bazzite checks passed")
sys.exit(1 if fails else 0)
