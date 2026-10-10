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
try:   # (the steps that did not do what they were to do, and how long they waited)
    for e in json.load(open(out / 'online.json')):
        if e.get('ok') is False or e['cmd'].startswith('wait'):
            print(f"STEP  {e['cmd']}: ok {e.get('ok')}, got {e.get('got')}, waited {e.get('waitedMs')} ms, began {e.get('began')}, at {e.get('wall')}")
except Exception:
    pass
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
# (2.18) the browser
br = reports('browse.json')
if br is None:
    check('the browser\'s run ran', False, 'no browse.json')
else:
    n, d, c = (br.get(l, {}).get('browse', {}) for l in ('new', 'detail', 'closed'))
    data = n.get('data', {})
    check('the browser: three channels from Google Takeout\'s list, their new videos from their feeds (newest first)',
          len(data.get('channels', [])) == 3 and n.get('count') == 20 and n.get('section') == 'new' and not n.get('error') and n.get('failedChannels') == 0,
          f"{len(data.get('channels', []))} channels, {n.get('count')} videos, feeds read {data.get('feedReads')}; first {n.get('titles', [])[:2]}; error '{n.get('error')}'")
    check('... with their pictures', data.get('thumbReads', 0) >= 12 and n.get('thumbsShown', 0) >= 12, f"{data.get('thumbReads')} fetched, {n.get('thumbsShown')} drawn")
    check('a video\'s page, and back out', d.get('detail') is True and d.get('detailButtons', [''])[0] == 'Play' and c.get('open') is False,
          f"{d.get('detailTitle')}: {d.get('detailButtons')}; closed {c.get('open') is False}")
net = reports('online-net.json')
if net is None:
    print('NET   the run that needs the internet did not finish')
else:
    f, pg, yt, se = (net.get(l, {}) for l in ('fetched', 'page', 'youtube', 'search'))
    sb = se.get('browse', {})
    print(f"NET   YouTube's search from the build machine, through the real yt-dlp.exe: {sb.get('count')} found, e.g. {sb.get('titles', [])[:3]}; error: {sb.get('error', '')[:300]}")
    fo, po, yo = f.get('online', {}), pg.get('online', {}), yt.get('online', {})
    print(f"NET   \"Get yt-dlp\" on Windows: {'fetched' if fo.get('fetches') else 'NOT fetched'}: {fo.get('status')} | {f.get('lastWarning')}")
    print(f"NET   a page of the mock site through the real yt-dlp.exe: {pg.get('state')}, {po.get('what')}, streams {po.get('player', {}).get('streams')}, at {pg.get('positionMs')} ms; error: {pg.get('lastError', '')[:300]}")
    print(f"NET   YouTube from the build machine: {yt.get('state')}, {yo.get('what')}, streams {yo.get('player', {}).get('streams')}, at {yt.get('positionMs')} ms, \"{yt.get('file')}\"; error: {yt.get('lastError', '')[:400]}")
print(f"\n{fails} web-video check(s) failed" if fails else "\nAll web-video checks passed")
sys.exit(1 if fails else 0)
