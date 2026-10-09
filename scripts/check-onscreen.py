#!/usr/bin/env python3
"""Checks what was really on the screen (tests/automation/onscreen.txt): grabs of the screen itself
are compared with what the player says it is showing.

  check-onscreen.py OUT_DIR NAME [NAME...] [--info-see-through]

NAME is the run's name (NAME.json and NAME-*.png in OUT_DIR). With --info-see-through, the
see-through checks of desk mode are reported but do not count as failures (a test machine whose
OpenGL cannot give a window a see-through background).
"""
import json, pathlib, sys
import numpy as np
from PIL import Image

args = [a for a in sys.argv[1:] if not a.startswith('--')]
info_see_through = '--info-see-through' in sys.argv
out = pathlib.Path(args[0]); names = args[1:]
fails = 0
LIST_BG = np.array([0x2a, 0x2d, 0x35], float)      # the list's background (Theme.cpp)
BACKDROP = np.array([255, 0, 255], float)          # "backdrop #ff00ff" in the script


def check(name, ok, detail, info=False):
    global fails
    if info:
        print(f"{'INFO-OK' if ok else 'INFO-NO'}  {name}: {detail}")
        return
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")


def rgb(path):
    return np.asarray(Image.open(path).convert('RGB')).astype(float)


def crop(img, rect, screen, scale):
    x0 = int(round((rect['x'] - screen['x']) * scale)); y0 = int(round((rect['y'] - screen['y']) * scale))
    x1 = int(round(x0 + rect['w'] * scale)); y1 = int(round(y0 + rect['h'] * scale))
    h, w = img.shape[:2]
    return img[max(0, y0):min(h, y1), max(0, x0):min(w, x1)]


def list_on_screen(run, rep, label, before, after, what):
    r = rep.get(label, {})
    if not r.get('lookListOpen') or 'lookListRect' not in r:
        check(f'{run}: {what}: the look list opens', False, f"open: {r.get('lookListOpen')}"); return
    scr, scale = r['screenRect'], r.get('screenScale', 1)
    a = crop(rgb(out / f'{run}-{after}.png'), r['lookListRect'], scr, scale)
    if a.size == 0:
        check(f'{run}: {what}: the look list is on the screen', False, f"outside the screen: {r['lookListRect']}"); return
    bg = (np.abs(a - LIST_BG).max(axis=2) < 14).mean()
    text = (a.min(axis=2) > 150).mean()          # the light lettering of its lines
    detail = f"{bg * 100:.0f}% of its place is the list's own background, {text * 100:.1f}% lettering"
    if before:
        b = crop(rgb(out / f'{run}-{before}.png'), r['lookListRect'], scr, scale)
        if b.shape == a.shape:
            detail += f", {np.abs(a - b).mean():.0f} levels different from before it opened"
    check(f'{run}: {what}: the look list is seen on the screen where the player has it', bg > 0.6 and text > 0.01, detail)


def desk_on_screen(run, rep, label, scene, screen, what):
    r = rep.get(label, {})
    s = np.asarray(Image.open(out / f'{run}-{scene}.png').convert('RGBA')).astype(float)
    g = rgb(out / f'{run}-{screen}.png')
    nw = r.get('deskNativeWindow')
    ox = oy = 0
    if nw:   # Windows: the view is the window's inside
        ox = nw['insideRect']['x'] - nw['monitorRect']['x']; oy = nw['insideRect']['y'] - nw['monitorRect']['y']
    h, w = s.shape[:2]
    g = g[oy:oy + h, ox:ox + w]
    if g.shape[:2] != (h, w):
        check(f'{run}: {what}: the desk view fits the screen grab', False, f'view {w}x{h} at {ox},{oy}, grab {g.shape[1]}x{g.shape[0]}'); return
    if r.get('deskControlsVisible'):
        check(f'{run}: {what}: the strip of controls is hidden for the grab', False, f"visible: {r.get('deskControls')}, report took {r.get('reportMs')} ms, steps: {[(e.get('began'), e.get('wall'), e['cmd'][:18]) for e in LOG[max(0, LOG.index(r) - 9):LOG.index(r) + 1]] if r in LOG else ''}")
    alpha = s[..., 3] / 255.0
    solid, clear, part = alpha > 0.996, alpha < 0.004, (alpha >= 0.004) & (alpha <= 0.996)
    near = (np.abs(g - s[..., :3]).max(axis=2) < 56)
    drawn = near[solid].mean() if solid.any() else 0
    check(f'{run}: {what}: the set is on the screen as the player drew it', solid.mean() > 0.03 and drawn > 0.85,
          f'{solid.mean() * 100:.0f}% of the view is the set; {drawn * 100:.0f}% of it matches on the screen')
    through = (np.abs(g - BACKDROP).max(axis=2) < 40)[clear].mean() if clear.any() else 0
    check(f'{run}: {what}: around the set the desktop behind shows (not black)', through > 0.99,
          f'{through * 100:.1f}% of the undrawn part shows the backdrop; average colour there {g[clear].mean(axis=0).round().tolist() if clear.any() else None}',
          info=info_see_through)
    if part.mean() > 0.002:
        a3 = alpha[..., None]
        want = a3 * s[..., :3] + (1 - a3) * BACKDROP
        err = np.abs(g - want)[part].mean()
        check(f'{run}: {what}: the shadow darkens the desktop behind (blended, not black)', err < 40,
              f'{part.mean() * 100:.1f}% of the view is half see-through; {err:.0f} levels from the expected blend', info=info_see_through)


def windows_checks(run, rep):
    fs = rep.get('fullscreen', {}); nw = fs.get('nativeWindow')
    if nw is None:
        return
    if nw.get('openGl'):
        check(f'{run}: Windows: the full-screen player (drawn with OpenGL) stays in the desktop\'s composition',
              nw.get('border') and not nw.get('insideIsWholeScreen') and fs.get('fullscreen') is True,
              f"border {nw.get('border')}, inside {nw.get('insideRect')}, screen {nw.get('monitorRect')}, asked in time {nw.get('borderAskedInTime')}")
    else:
        check(f'{run}: Windows: the full-screen player (not drawn with OpenGL) covers the whole screen',
              not nw.get('border') and nw.get('insideIsWholeScreen'), f"border {nw.get('border')}, inside {nw.get('insideRect')}")
    for label, what in (('desk', 'desk mode'), ('desk-ontop', 'desk mode kept on top'), ('desk-ontop-off', 'desk mode, no longer on top'),
                        ('desk-again', 'desk mode, second visit')):
        d = rep.get(label, {}).get('deskNativeWindow') or {}
        check(f'{run}: Windows: {what}: the see-through window stays in the desktop\'s composition',
              d.get('border') and not d.get('insideIsWholeScreen') and d.get('openGl') and d.get('alphaAsked', 0) >= 8 and d.get('visible'),
              f"border {d.get('border')}, inside {d.get('insideRect')}, screen {d.get('monitorRect')}, layered {d.get('layered')}, "
              f"alpha asked {d.get('alphaAsked')} got {d.get('alphaGot')} (pixel format {d.get('pixelFormat')}: {d.get('alphaBits')}), "
              f"shaped {d.get('shaped')} {d.get('shapeBox')}, topmost {d.get('topmost')}, style {d.get('style')}/{d.get('exStyle')}")
    top = rep.get('desk-ontop', {}).get('deskNativeWindow') or {}
    check(f'{run}: Windows: "keep on top" is set on the desk window', top.get('topmost') is True, top.get('topmost'))


for run in names:
    try:
        log = json.load(open(out / f'{run}.json'))
        LOG = log
    except Exception as e:
        check(f'{run}: the run finished', False, str(e)); continue
    rep = {e['label']: e for e in log if e['cmd'] == 'report'}
    check(f'{run}: the run finished', 'end' in rep, f"{len(rep)} reports; Qt {rep.get('end', {}).get('qtVersion')}; OpenGL: {rep.get('end', {}).get('gl')}; main window drawn with OpenGL: "
          f"{(rep.get('end', {}).get('nativeWindow') or {}).get('openGl', 'n/a')}")
    list_on_screen(run, rep, 'window-list', 'window-before', 'window-list', 'in a window')
    list_on_screen(run, rep, 'full-list', 'full-before', 'full-list', 'full screen')
    list_on_screen(run, rep, 'desk-list', 'desk-screen', 'desk-list', 'desk mode')
    desk_on_screen(run, rep, 'desk', 'desk-scene', 'desk-screen', 'desk mode')
    desk_on_screen(run, rep, 'desk-ontop', 'ontop-scene', 'ontop-screen', 'desk mode kept on top')
    desk_on_screen(run, rep, 'desk-again', 'again-scene', 'again-screen', 'desk mode, second visit')
    windows_checks(run, rep)

print(f"\n{fails} on-screen check(s) failed" if fails else "\nAll on-screen checks passed")
sys.exit(1 if fails else 0)
