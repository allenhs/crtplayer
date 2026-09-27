#!/usr/bin/env python3
"""MPRIS end to end: a private session bus, the player, and playerctl (the client KDE's
media widget and KDE Connect behave like). Usage: check-mpris.py BINARY MEDIA_DIR"""
import os, shutil, subprocess, sys, tempfile, time

binary, media = sys.argv[1], sys.argv[2]
if not shutil.which('playerctl') or not shutil.which('dbus-daemon'):
    print('SKIP  MPRIS: playerctl or dbus-daemon not installed'); sys.exit(0)
fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
bus = subprocess.run(['dbus-daemon', '--session', '--fork', '--print-address=1', '--print-pid=1'], capture_output=True, text=True).stdout.split()
env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=bus[0], XDG_CONFIG_HOME=tempfile.mkdtemp(), XDG_DATA_HOME=tempfile.mkdtemp())
tmp = tempfile.mkdtemp()
script = os.path.join(tmp, 'm.txt')
open(script, 'w').write('waitstate playing 15000\nwait 20000\nquit\n')
mon = subprocess.Popen(['dbus-monitor', "type='signal',member='PropertiesChanged',path='/org/mpris/MediaPlayer2'"], env=env,
                       stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True)
app = subprocess.Popen([binary, '--automation', script, os.path.join(media, 'sd_4x3_h264.mp4'), os.path.join(media, 'hd_16x9_multitrack.mkv')],
                       env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
def pc(*a):
    return subprocess.run(['playerctl', '-p', 'crtplayer', *a], env=env, capture_output=True, text=True, timeout=10).stdout.strip()
time.sleep(4)
players = subprocess.run(['playerctl', '-l'], env=env, capture_output=True, text=True).stdout.split()
check('the player is on the bus as "crtplayer"', 'crtplayer' in players, ' '.join(players) or 'none')
check('status and metadata', pc('status') == 'Playing' and pc('metadata', 'xesam:title') == 'sd_4x3_h264' and pc('metadata', 'mpris:length') == '20000000',
      f"{pc('status')}, '{pc('metadata', 'xesam:title')}', {pc('metadata', 'mpris:length')} us")
pc('pause'); time.sleep(1); paused = pc('status')
pc('play'); time.sleep(1); playing = pc('status')
check('pause / play', paused == 'Paused' and playing == 'Playing', f'{paused}, {playing}')
pc('position', '10'); time.sleep(1.2)
p = float(pc('position') or 0)
check('set position', 10 <= p <= 12.5, f'{p:.1f} s after setting 10 s')
pc('volume', '0.3'); time.sleep(0.6)
check('volume', abs(float(pc('volume') or 0) - 0.3) < 0.011, pc('volume'))
pc('next'); time.sleep(2.5); t1 = pc('metadata', 'xesam:title')
pc('previous'); time.sleep(2.5); t2 = pc('metadata', 'xesam:title')
check('next / previous', t1 == 'hd_16x9_multitrack' and t2 == 'sd_4x3_h264', f'{t1}, {t2}')
app.wait(timeout=40)
mon.terminate()
sig = mon.communicate(timeout=5)[0]
n = sig.count('member=PropertiesChanged')
check('changes are announced (PropertiesChanged)', n >= 6 and '"PlaybackStatus"' in sig and '"Metadata"' in sig, f'{n} signals')
subprocess.run(['kill', bus[1]])
print(f"\n{fails} MPRIS check(s) failed" if fails else "\nAll MPRIS checks passed")
sys.exit(1 if fails else 0)
