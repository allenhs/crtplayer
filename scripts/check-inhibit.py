#!/usr/bin/env python3
"""Keep-awake: the player asks the desktop not to dim/sleep while playing and releases it
on pause and on quit. Runs against stand-ins for KDE's services and for the desktop portal.
Usage: check-inhibit.py BINARY MEDIA_DIR"""
import json, os, shutil, subprocess, sys, tempfile, time, pathlib
binary, media = sys.argv[1], sys.argv[2]
here = pathlib.Path(__file__).resolve().parent.parent
if not shutil.which('dbus-daemon'):
    print('SKIP  keep-awake: dbus-daemon not installed'); sys.exit(0)
fails = 0

def dbus_python():
    """A Python that can load the D-Bus and GLib bindings. Distribution packages (python3-dbus,
    python3-gi) are built for the system's own Python, which may not be the default python3."""
    import shutil
    for cand in [sys.executable, 'python3', 'python3.12', 'python3.13', 'python3.11', 'python3.10']:
        exe = shutil.which(cand) or cand
        if subprocess.run([exe, '-c', 'import dbus, dbus.mainloop.glib; from gi.repository import GLib'],
                          capture_output=True).returncode == 0:
            return exe
    return sys.executable   # none works: the mock's own error will say why

def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
script = ('open ' + os.path.join(media, 'sd_4x3_h264.mp4') + '\nwaitstate playing 15000\nwait 1500\nreport playing\n'
          'pause\nwait 1500\nreport paused\nplay\nwait 1500\nreport playing-again\nquit\n')
for mode in ('kde', 'portal'):
    tmp = tempfile.mkdtemp()
    bus = subprocess.run(['dbus-daemon', '--session', '--fork', '--print-address=1', '--print-pid=1'], capture_output=True, text=True).stdout.split()
    env = dict(os.environ, DBUS_SESSION_BUS_ADDRESS=bus[0], XDG_CONFIG_HOME=tempfile.mkdtemp(), XDG_DATA_HOME=tempfile.mkdtemp())
    log = os.path.join(tmp, 'calls.jsonl')
    mock = subprocess.Popen([dbus_python(), str(here / 'tests' / 'inhibit_mock.py'), mode, log], env=env)
    for _ in range(50):
        if os.path.exists(log): break
        time.sleep(0.1)
    open(os.path.join(tmp, 's.txt'), 'w').write(script)
    subprocess.run([binary, '--automation', os.path.join(tmp, 's.txt'), '--automation-log', os.path.join(tmp, 'a.json')],
                   env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=90)
    time.sleep(0.5)
    mock.terminate(); subprocess.run(['kill', bus[1]])
    calls = [json.loads(l) for l in open(log) if '"call"' in l]
    rep = {e['label']: e for e in json.load(open(os.path.join(tmp, 'a.json'))) if e['cmd'] == 'report'}
    seq = [f"{c['iface']}.{c['call']}" for c in calls]
    if mode == 'kde':
        inh = [c for c in calls if c['iface'] == 'ScreenSaver' and c['call'] == 'Inhibit']
        un = [c for c in calls if c['iface'] == 'ScreenSaver' and c['call'] == 'UnInhibit']
        check('KDE: screen kept awake while playing', rep['playing']['sleepInhibited'] and 'ScreenSaver' in rep['playing']['sleepInhibitMethod'],
              rep['playing']['sleepInhibitMethod'])
        check('KDE: released on pause', not rep['paused']['sleepInhibited'] and len(un) >= 1 and un[0]['cookie'] == inh[0]['cookie'],
              f"UnInhibit({un[0]['cookie'] if un else '-'}) for Inhibit -> {inh[0]['cookie'] if inh else '-'}")
        check('KDE: taken again on play, released on quit', len(inh) == 2 and len(un) == 2 and un[1]['cookie'] == inh[1]['cookie'],
              ' '.join(seq))
        pm = [c for c in calls if c['iface'] == 'PowerManagement']
        check('KDE: automatic suspend blocked too (PowerManagement)', [c['call'] for c in pm] == ['Inhibit', 'UnInhibit', 'Inhibit', 'UnInhibit'],
              ' '.join(c['call'] for c in pm))
        check('KDE: says who and why', inh and inh[0]['app'] == 'CRT Player' and inh[0]['reason'] == 'Playing a video',
              f"{inh[0]['app']!r}, {inh[0]['reason']!r}" if inh else '-')
    else:
        inh = [c for c in calls if c['call'] == 'Inhibit']
        close = [c for c in calls if c['call'] == 'Close']
        check('portal (GNOME and others): idle and suspend inhibited while playing',
              rep['playing']['sleepInhibited'] and inh and inh[0]['flags'] == 12, f"flags {inh[0]['flags'] if inh else '-'} (4 suspend + 8 idle)")
        check('portal: released on pause and on quit (Request.Close)', len(inh) == 2 and len(close) == 2 and close[0]['handle'] == inh[0]['handle'],
              ' '.join(seq))
print(f"\n{fails} keep-awake check(s) failed" if fails else "\nAll keep-awake checks passed")
sys.exit(1 if fails else 0)
