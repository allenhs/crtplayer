#!/usr/bin/env python3
"""Checks a Jellyfin test run: the mock server's request log, the automation logs and the
stored session file. Usage: check-jellyfin.py OUT_DIR VERSION SESSION_FILE"""
import json, os, re, stat, sys, pathlib

out, version, session = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  [{version}] {name}: {detail}")

reqs = [json.loads(l) for l in open(out / 'requests.jsonl')]
run1 = json.load(open(out / 'jellyfin.json'))
run2 = json.load(open(out / 'jellyfin-restore.json'))
run3 = json.load(open(out / 'jellyfin-transcode.json')) if (out / 'jellyfin-transcode.json').exists() else []
rep = {e['label']: e for e in run1 + run2 + run3 if e['cmd'] == 'report'}
modern = [int(x) for x in version.split('.')[:2]] >= [10, 9]

bad = [r for r in reqs if r['path'] == '/Users/AuthenticateByName' and r['status'] == 401]
check('wrong password is rejected and leaves the player signed out', bad and not rep['jf-bad-password']['jellyfinSignedIn'], f'{len(bad)} rejected attempt(s)')
check('correct password signs in', rep['jf-home']['jellyfinSignedIn'] and rep['jf-home']['jellyfinListingCount'] >= 2,
      f"home shows {rep['jf-home']['jellyfinListingCount']} entries")
api = [r for r in reqs if r['path'] not in ('/System/Info/Public', '/Users/AuthenticateByName')]
HLS = re.compile(r'^/videos/\w+/(master\.m3u8|main\.m3u8|hls1/)', re.I)
# HLS playlists and segments of a conversion are fetched by GStreamer's HLS reader, which
# sends no custom headers: they are authenticated by the address (checked below).
hdr = [r for r in api if not (HLS.match(r['path']) and not r['path'].lower().endswith('master.m3u8'))]
check('every authenticated request carries the Authorization header (except HLS segments)', all(r['authHeader'] for r in hdr),
      f'{len(hdr)} requests' + (f", {len(api) - len(hdr)} HLS playlist/segment requests by address" if len(api) > len(hdr) else ''))
# The only addresses allowed to carry the token: the server's conversions (HLS playlists and
# segments, fetched without our headers) and the subtitle files it extracts from them.
tokened = [r for r in reqs if r['tokenInUrl']]
check('the token appears in no address except a conversion\'s (HLS) and its subtitle files',
      all(HLS.match(r['path']) or '/Subtitles/' in r['path'] for r in tokened),
      f"{len(tokened)} such requests, all HLS or its subtitles" if tokened else 'no api_key/token query parameters')
check('original-file streams never carry the token', not any(r['tokenInUrl'] for r in reqs if r['path'].endswith('/stream')), 'header only')
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
a = rep.get('jf-auto-subs')
check('with subtitles on, a video without its own gets the server\'s subtitle file by itself', a is not None and a['externalSubtitle'] == 'English (SRT, external)'
      and a['currentSubtitle'] >= 0 and a['state'] == 'playing', f"{a['externalSubtitle'] if a else 'missing'}; {a['state'] if a else ''}")
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

# ---- 2.8: paged libraries and conversion on the server (tests/automation/jellyfin-transcode.txt)
if run3:
    t3 = (out / 'jellyfin-transcode.json').read_text() + (out / 'jellyfin-transcode.log').read_text()
    failed = [e for e in run3 if e.get('ok') is False]
    check('the paging and conversion script ran without a failed step', not failed, ', '.join(e['cmd'] for e in failed) or 'all steps ok')
    bf, ba = rep['jf-big-first'], rep['jf-big-all']
    check('a big library (1,234 items) opens with its first page only', bf['jellyfinListingCount'] == 100 and bf['jellyfinListingTotal'] == 1234,
          f"{bf['jellyfinListingCount']} shown of {bf['jellyfinListingTotal']}")
    pages = [r for r in reqs if r['query'].get('ParentId') == 'lib-big']
    starts = sorted(int(r['query'].get('StartIndex', 0)) for r in pages)
    check('scrolling loads the rest a page at a time, each page once', ba['jellyfinListingCount'] == 1234 and starts == list(range(0, 1234, 100))
          and all(r['query'].get('Limit') == '100' for r in pages), f"{ba['jellyfinListingCount']} items from {len(pages)} requests (StartIndex {starts[0]}..{starts[-1]})")
    check('the last item of the big library plays (beyond the old 500 limit)', rep['jf-big-last']['state'] == 'playing'
          and rep['jf-big-last']['file'].startswith('Clip 1234'), rep['jf-big-last']['file'])
    pinfo = [r for r in reqs if r['path'].endswith('/PlaybackInfo')]
    prof = [r['body'].get('DeviceProfile', {}).get('DirectPlayProfiles', [{}])[0] for r in pinfo]
    check('every play asks the server how (PlaybackInfo) with this computer\'s formats',
          pinfo and all('h264' in p.get('VideoCodec', '').split(',') and 'prores' not in p.get('VideoCodec', '') and 'mkv' in p.get('Container', '') for p in prof),
          f"{len(pinfo)} requests; video codecs: {prof[0].get('VideoCodec') if prof else '-'}")
    cc = rep['jf-converted-codec']
    check('a codec this computer can\'t decode is converted by the server', cc['state'] == 'playing' and cc['jellyfinPlayMethod'] == 'Transcode'
          and 'VideoCodecNotSupported' in cc['jellyfinTranscodeReasons'], f"{cc['jellyfinPlayMethod']}, {cc['jellyfinTranscodeReasons']}")
    segs = [r for r in reqs if '/hls1/' in r['path']]
    check('the conversion streams as HLS segments, authenticated by their address', segs and all(r['status'] == 200 for r in segs),
          f"{len(segs)} segments, all 200")
    check('seeking works in a converted video', rep['jf-converted-seek']['positionMs'] >= 11500, f"{rep['jf-converted-seek']['positionMs']:.0f} ms")
    fb = rep['jf-fallback']
    forced = [r for r in pinfo if '/m5/' in r['path'] and r['body'].get('EnableDirectPlay') is False]
    check('an original that fails here is retried converted, once', fb['state'] == 'playing' and fb['jellyfinPlayMethod'] == 'Transcode'
          and fb['jellyfinConvertedAfterFailure'] and len(forced) == 1 and 'Details' not in fb.get('lastError', ''),
          f"{fb['jellyfinPlayDescription']}; {len(forced)} conversion request(s)")
    ql = rep['jf-quality-limit']
    capped = [r for r in pinfo if '/m2/' in r['path']]
    check('over the quality limit is converted; the limit reaches the server', ql['jellyfinPlayMethod'] == 'Transcode'
          and 'ContainerBitrateExceedsLimit' in ql['jellyfinTranscodeReasons'] and capped and capped[-1]['body']['MaxStreamingBitrate'] == 4_000_000,
          f"{ql['jellyfinTranscodeReasons']}, MaxStreamingBitrate {capped[-1]['body']['MaxStreamingBitrate'] if capped else '-'}")
    check('a converted video resumes at its saved position', ql['positionMs'] >= 12000, f"{ql['positionMs']:.0f} ms (saved: 12000)")
    check('its embedded subtitle is offered as a file from the server', rep['jf-converted-subtitle']['externalSubtitle'] == 'English (embedded)',
          rep['jf-converted-subtitle']['externalSubtitle'])
    check('"Play converted by the server" converts a playable file', rep['jf-forced']['jellyfinPlayMethod'] == 'Transcode'
          and rep['jf-forced']['state'] == 'playing', rep['jf-forced']['jellyfinPlayDescription'])
    check('back at the original quality, the same file plays directly again', rep['jf-direct-again']['jellyfinPlayMethod'] == 'DirectPlay'
          and rep['jf-direct-again']['state'] == 'playing', rep['jf-direct-again']['jellyfinPlayDescription'])
    tstarts = [r['body'] for r in reqs if r['path'] == '/Sessions/Playing' and r['body'].get('PlayMethod') == 'Transcode']
    ended = {r['query'].get('playSessionId') for r in reqs if r['path'] == '/Videos/ActiveEncodings' and r['method'] == 'DELETE'}
    check('converted playback is reported as such, with the server\'s play session', len(tstarts) >= 4 and all(b['PlaySessionId'].startswith('ps-') for b in tstarts),
          f"{len(tstarts)} 'Transcode' start reports")
    check('each conversion is ended on the server when playback moves on', all(b['PlaySessionId'] in ended for b in tstarts),
          f"{len(ended)} ended, for {len(tstarts)} converted plays")
    check('the token is never written to the logs', 'tok-' not in t3 and 'api_key' not in t3, 'automation log and output checked')
print(f"\n{fails} Jellyfin check(s) failed" if fails else "\nAll Jellyfin checks passed")
sys.exit(1 if fails else 0)
