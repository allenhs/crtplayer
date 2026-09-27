#!/usr/bin/env python3
"""Checks a Jellyfin test run: the mock server's request log, the automation logs and the
stored session file. Usage: check-jellyfin.py OUT_DIR VERSION SESSION_FILE"""
import json, os, stat, sys, pathlib

out, version, session = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  [{version}] {name}: {detail}")

reqs = [json.loads(l) for l in open(out / 'requests.jsonl')]
run1 = json.load(open(out / 'jellyfin.json'))
run2 = json.load(open(out / 'jellyfin-restore.json'))
rep = {e['label']: e for e in run1 + run2 if e['cmd'] == 'report'}
modern = [int(x) for x in version.split('.')[:2]] >= [10, 9]

bad = [r for r in reqs if r['path'] == '/Users/AuthenticateByName' and r['status'] == 401]
check('wrong password is rejected and leaves the player signed out', bad and not rep['jf-bad-password']['jellyfinSignedIn'], f'{len(bad)} rejected attempt(s)')
check('correct password signs in', rep['jf-home']['jellyfinSignedIn'] and rep['jf-home']['jellyfinListingCount'] >= 2,
      f"home shows {rep['jf-home']['jellyfinListingCount']} entries")
api = [r for r in reqs if r['path'] not in ('/System/Info/Public', '/Users/AuthenticateByName')]
check('every authenticated request carries the Authorization header', all(r['authHeader'] for r in api), f'{len(api)} requests')
check('the token never appears in a URL', not any(r['tokenInUrl'] for r in reqs), 'no api_key/token query parameters')
# 499 = the player closed the connection mid-transfer (normal when switching videos).
closed = [r for r in api if r['status'] == 499]
check('no request failed', all(r['status'] < 400 or r['status'] == 499 for r in api if r['path'] != '/Users/AuthenticateByName'),
      (', '.join(sorted({f"{r['path']}={r['status']}" for r in api if r['status'] >= 400 and r['status'] != 499})) or 'all 2xx')
      + (f' ({len(closed)} stream(s) closed by the player when switching videos)' if closed else ''))
check('closed streams had started successfully first', all(any(r2['path'] == r['path'] and r2['status'] in (200, 206) for r2 in reqs) for r in closed),
      f'{len(closed)} closed')
views = [r['path'] for r in reqs if 'View' in r['path']]
want = '/UserViews' if modern else '/Users/u1/Views'
check('uses the route set matching the server version', views and all(p == want for p in views), f'{sorted(set(views))}')
streams = [r for r in reqs if r['path'].startswith('/Videos/') and r['path'].endswith('/stream')]   # not subtitle downloads
ranged = [r for r in streams if r['range'] and not r['range'].startswith('bytes=0-')]
check('streams the original file and seeks with HTTP ranges', streams and ranged,
      f"{len(streams)} stream requests, {len(ranged)} mid-file range requests")
check('stream request marks the file as static (no server transcoding)', all(r['query'].get('static') == 'true' for r in streams), 'static=true')
check('seek moved playback to ~12 s', rep['jf-after-seek']['positionMs'] >= 11500, f"{rep['jf-after-seek']['positionMs']:.0f} ms")
check('continue-watching item resumed at its saved position', rep['jf-resumed']['positionMs'] >= 12000,
      f"{rep['jf-resumed']['positionMs']:.0f} ms (saved: 12000)")
sess = [(r['path'], r.get('body', {})) for r in reqs if r['path'].startswith('/Sessions/Playing')]
starts = [b for p, b in sess if p == '/Sessions/Playing']
progress = [b for p, b in sess if p.endswith('Progress')]
stops = [b for p, b in sess if p.endswith('Stopped')]
check('playback start reported', len(starts) >= 3, f'{len(starts)} start reports')
check('pause/unpause reported', any(b.get('EventName') == 'pause' for b in progress) and any(b.get('EventName') == 'unpause' for b in progress),
      f'{len(progress)} progress reports')
m1stop = [b for b in stops if b.get('ItemId') == 'm1']
check('stop reports the position (resume point) for the first movie', m1stop and m1stop[0]['PositionTicks'] >= 120_000_000,
      f"{m1stop[0]['PositionTicks'] / 1e7:.1f} s" if m1stop else 'missing')
check('TV show navigation reaches episodes', rep['jf-episode']['source'] == 'jellyfin' and abs(rep['jf-episode']['displayAspect'] - 2.3704) < 0.01,
      f"{rep['jf-episode']['file']}")
subreq = [r for r in reqs if '/Subtitles/' in r['path']]
check('external subtitle fetched from the server, authenticated', subreq and all(r['authHeader'] and r['status'] == 200 for r in subreq),
      f"{len(subreq)} subtitle request(s): {subreq[0]['path'] if subreq else '-'}")
check('session restored on the next launch', rep['jf-restored']['jellyfinSignedIn'], 'signed in without the password')
logout = [r for r in reqs if r['path'] == '/Sessions/Logout']
check('signing out revokes the token on the server', logout and logout[0]['status'] == 204 and not rep['jf-signed-out']['jellyfinSignedIn'],
      f'{len(logout)} logout request(s)')
check('no password is ever written to the automation logs', 'crt"' not in (out / 'jellyfin.json').read_text() and 'wrong-password' not in (out / 'jellyfin.json').read_text(), 'checked')
if os.path.exists(session):
    mode = stat.S_IMODE(os.stat(session).st_mode)
    data = json.load(open(session))
    check('session file is private (0600)', mode == 0o600, oct(mode))
    check('session file never holds a password', 'crt' not in json.dumps(data).replace('CRT', '') and not any('pw' in k.lower() or 'pass' in k.lower() for k in data), 'no password field or value')
    check('after sign-out the stored token is gone', data.get('token', '') == '', 'token cleared')
else:
    check('session file exists', False, session)
print(f"\n{fails} Jellyfin check(s) failed" if fails else "\nAll Jellyfin checks passed")
sys.exit(1 if fails else 0)
