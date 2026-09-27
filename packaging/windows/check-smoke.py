#!/usr/bin/env python3
"""Checks the Windows smoke test's results. Usage: check-smoke.py OUT_DIR"""
import json, sys, pathlib
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
ver = (out / 'version.txt').read_text(errors='replace').strip()
check('the packaged player starts on its own', 'CRTPlayer' in ver, ver or '(no output)')
gst = (out / 'gstreamer.txt').read_text(errors='replace')
check('its own GStreamer is complete (nothing missing, every format covered)',
      'Missing essential elements: none' in gst and 'Everything for common formats is installed' in gst, gst.strip().replace('\n', ' | ')[:400])
try:
    log = json.load(open(out / 'smoke.json'))
except Exception as e:
    check('the automation ran', False, str(e)); print(f"\n{fails} Windows check(s) failed"); sys.exit(1)
rep = {e['label']: e for e in log if e['cmd'] == 'report'}
p = rep.get('playing', {})
check('it plays (H.264 video, Vorbis audio)', p.get('state') == 'playing' and p.get('positionMs', 0) > 500,
      f"{p.get('state')}, {p.get('positionMs')} ms, video {p.get('videoDecoder')}, audio {p.get('audioDecoder')}"
      f" to {p.get('audioOutput')}; error: {p.get('lastError')}")
check('keep awake: Windows keeps the display on while playing', p.get('sleepInhibitMethod') == 'SetThreadExecutionState',
      p.get('sleepInhibitMethod'))
check('the look\'s sound switches on', rep.get('vhs', {}).get('tapeSoundActive') is True, rep.get('vhs', {}).get('tapeSoundActive'))
check('released when paused', rep.get('paused', {}).get('sleepInhibitMethod') == 'none', rep.get('paused', {}).get('sleepInhibitMethod'))
try:
    import numpy as np
    from PIL import Image
    img = lambda n: np.asarray(Image.open(out / n).convert('RGB')).astype(float)
    a = img('playing.png')
    check('the picture is drawn (with the CRT look)', a.mean() > 20 and a.std() > 20, f'mean {a.mean():.0f}, contrast {a.std():.0f}')
    s = img('subtitle.png'); h = s.shape[0]
    lower = s[int(h * 0.75):]
    white = ((lower > 200).all(axis=2)).mean()
    check('a subtitle file is drawn (the text plugin is bundled)', white > 0.001, f'{white * 100:.2f}% white text pixels in the bottom quarter')
    d = np.asarray(Image.open(out / 'desk.png').convert('RGBA')).astype(float)
    check('desk mode draws the arcade cabinet', rep.get('desk', {}).get('deskCabinetDrawn') == 'arcade' and (d[..., 3] > 0).mean() > 0.05,
          f"{rep.get('desk', {}).get('deskCabinetDrawn')}, {(d[..., 3] > 0).mean() * 100:.0f}% of the view drawn")
except ImportError as e:
    check('image checks', False, f'Pillow/numpy missing: {e}')
print(f"\n{fails} Windows check(s) failed" if fails else "\nAll Windows checks passed")
sys.exit(1 if fails else 0)
