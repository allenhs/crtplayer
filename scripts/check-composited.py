#!/usr/bin/env python3
"""What the compositor actually SHOWS (not what the player renders): runs the player in a
Weston with its screenshot extension and requires a colourful picture in the video area.
Screenshots are retried until the first frame appears (start-up, e.g. an AppImage
unpacking itself, takes a variable time). Usage: check-composited.py BINARY MEDIA_DIR NAME"""
import os, shutil, subprocess, sys, tempfile, time, glob
import numpy as np
from PIL import Image
binary, media, name = sys.argv[1], sys.argv[2], sys.argv[3]
if not (shutil.which('weston') and shutil.which('weston-screenshooter')):
    print('SKIP  composited screen: weston / weston-screenshooter not installed'); sys.exit(0)
rt = tempfile.mkdtemp(); os.chmod(rt, 0o700)
env = dict(os.environ, XDG_RUNTIME_DIR=rt, WAYLAND_DISPLAY='wl-composited', QT_QPA_PLATFORM='wayland',
           XDG_CONFIG_HOME=tempfile.mkdtemp(), XDG_DATA_HOME=tempfile.mkdtemp())
env.pop('DISPLAY', None)
w = subprocess.Popen(['weston', '--backend=headless', '--renderer=gl', '--width=1280', '--height=800',
                      '--socket=wl-composited', '--debug', '--idle-time=0'], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(3)
script = os.path.join(rt, 's.txt')
open(script, 'w').write(f"open {os.path.join(media, 'sd_4x3_h264.mp4')}\nwaitstate playing 20000\nwait 25000\nquit\n")
app = subprocess.Popen([binary, '--automation', script], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
best, t0, picture = None, time.time(), False
while time.time() - t0 < 30 and app.poll() is None:
    time.sleep(1.5)
    for f in glob.glob(os.path.join(rt, 'wayland-screenshot*.png')): os.remove(f)
    subprocess.run(['weston-screenshooter'], env=env, cwd=rt, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=10)
    shots = glob.glob(os.path.join(rt, 'wayland-screenshot*.png'))
    if not shots: continue
    a = np.asarray(Image.open(shots[0]).convert('RGB')).astype(float)[120:680, 120:1160]
    colour = float(a.std(axis=2).mean())            # test bars: strongly coloured
    best = max(best or 0.0, colour)
    if colour > 25 and a.mean() > 30:
        picture = True
        shutil.copy(shots[0], os.path.join(os.getcwd(), f'composited-{name}.png')) if os.access(os.getcwd(), os.W_OK) else None
        break
app.kill(); w.kill()
ok = picture
print(f"{'PASS' if ok else 'FAIL'}  composited screen shows the video ({name}): "
      f"{'picture after %.0f s' % (time.time() - t0) if ok else 'no picture within 30 s (best colourfulness %.1f)' % (best or 0)}")
sys.exit(0 if ok else 1)
