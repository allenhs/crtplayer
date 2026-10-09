#!/usr/bin/env python3
"""Checks the Windows smoke test's web-video runs. Usage: check-smoke-online.py OUT_DIR
The first run (two streams over HTTP from the mock site) counts. The second (the internet: fetching yt-dlp,
YouTube itself) is only reported: its lines begin with NET."""
import json, sys, pathlib
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
def reports(name):
    try: return {e['label']: e for e in json.load(open(out / name)) if e['cmd'] == 'report'}
    except Exception as e: return None
rep = reports('online.json')
if rep is None:
    check('the web-video run ran', False, 'no online.json')
else:
    p, j, b, d = (rep.get(l, {}) for l in ('pair', 'pair-jump', 'pair-back', 'direct'))
    web = p.get('online', {}).get('player', {})
    check('a video whose picture and sound come from two addresses plays (over HTTP, each through its buffer)',
          p.get('state') == 'playing' and web.get('streams') == 2 and p.get('positionMs', 0) > 1500 and p.get('everyday', {}).get('soundLevelDb', -120) > -60,
          f"{p.get('state')}, {web.get('kinds')}, at {p.get('positionMs')} ms, sound {p.get('everyday', {}).get('soundLevelDb')} dB,"
          f" video {p.get('videoDecoder')}, audio {p.get('audioDecoder')}; error: {p.get('lastError')}")
    check('a jump ahead and one back: both streams go there', j.get('state') == 'playing' and 12000 <= j.get('positionMs', 0) <= 17000 and j.get('everyday', {}).get('soundLevelDb', -120) > -60
          and b.get('state') == 'playing' and 3000 <= b.get('positionMs', 0) <= 8000 and b.get('everyday', {}).get('soundLevelDb', -120) > -60,
          f"at {j.get('positionMs')} ms ({j.get('everyday', {}).get('soundLevelDb')} dB), then at {b.get('positionMs')} ms ({b.get('everyday', {}).get('soundLevelDb')} dB)")
    check('a video file\'s address plays as it is', d.get('state') == 'playing' and d.get('file') == 'v_h264.mp4', f"{d.get('state')}, {d.get('file')}; error: {d.get('lastError')}")
net = reports('online-net.json')
if net is None:
    print('NET   the run that needs the internet did not finish')
else:
    f, pg, yt = (net.get(l, {}) for l in ('fetched', 'page', 'youtube'))
    fo, po, yo = f.get('online', {}), pg.get('online', {}), yt.get('online', {})
    print(f"NET   \"Get yt-dlp\" on Windows: {'fetched' if fo.get('fetches') else 'NOT fetched'}: {fo.get('status')} | {f.get('lastWarning')}")
    print(f"NET   a page of the mock site through the real yt-dlp.exe: {pg.get('state')}, {po.get('what')}, streams {po.get('player', {}).get('streams')}, at {pg.get('positionMs')} ms; error: {pg.get('lastError', '')[:300]}")
    print(f"NET   YouTube from the build machine: {yt.get('state')}, {yo.get('what')}, streams {yo.get('player', {}).get('streams')}, at {yt.get('positionMs')} ms, \"{yt.get('file')}\"; error: {yt.get('lastError', '')[:400]}")
print(f"\n{fails} web-video check(s) failed" if fails else "\nAll web-video checks passed")
sys.exit(1 if fails else 0)
