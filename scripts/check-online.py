#!/usr/bin/env python3
"""Checks the runs of "videos from web sites" (tests/automation/online*.txt, 2.17).
Usage: check-online.py OUT_DIR SITE      (SITE: the mock site's address, http://127.0.0.1:PORT)

The clips (scripts/make-test-media.sh, web/) say in every picture where in the video it is: a white bar along
the top grows 5 pixels a second. Their sound says it too: ten seconds of tone, ten of silence, in turn. Their
subtitle lines can be counted: line N is N letters "O" set wide apart. So every screenshot and every report
is judged by itself."""
import json, re, sys, pathlib
import numpy as np
from PIL import Image

out = pathlib.Path(sys.argv[1]); site = sys.argv[2]; fails = 0
LINES = {'a': [(2, 6, 1), (10, 20, 2), (25, 28, 3), (30, 45, 4), (50, 55, 5), (60, 110, 6), (112, 118, 7)],
         'b': [(0, 58, 8), (62, 119, 9)], 'n': []}

def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

def measure(path):
    """(seconds into the video or None, letters counted or -1)"""
    a = np.asarray(Image.open(path).convert('RGB'), dtype=int)
    h, w, _ = a.shape
    s = w / 640.0
    plain = a[int(60 * s):int(150 * s)].reshape(-1, 3).mean(axis=0)
    if np.abs(plain - np.array([0x20, 0x38, 0x60])).max() > 40: return None, -1   # not the test video's picture
    white = a[int(6 * s)].min(axis=1) > 180
    n = 0
    while n < w and white[n]: n += 1
    if w != 640: return n / (5.0 * s), -1                    # (the large clip: noise below, no subtitles)
    cols = (a[int(24 * s):].max(axis=2) > 150).any(axis=0)   # (the letters; not their dark outline)
    runs, gap, inrun = 0, 99, False
    for c in cols:
        if c:
            if not inrun and gap >= int(8 * s): runs += 1
            inrun, gap = True, 0
        else:
            inrun = False; gap += 1
    return n / (5.0 * s), runs

def wanted(rule, t):
    res = set()
    for dt in (-0.3, 0, 0.3): res.add(next((n for a, b, n in LINES[rule] if a <= t + dt < b), 0))
    return res

def load(run):
    f = out / (run + '.json')
    if not f.exists(): return None, None
    log = json.load(open(f))
    reports = {}
    for e in log:
        if e['cmd'] == 'report': reports.setdefault(e['label'], []).append(e)
    return log, reports

def steps_ok(run, log):
    bad = [e.get('line', e['cmd']) for e in log if e.get('ok', True) is False]
    done = log[-1]['cmd'] == 'finished' and log[-1].get('failures') == 0
    check(f'{run}: every step of the run did what it was to do', not bad and done, f"{len(log)} steps" + (f"; not: {bad[:4]}" if bad else '') + ('' if done else '; the run did not finish cleanly'))

def pictures(run, what):
    files = sorted((out / run).glob('[abn]_*_*.png'))
    bad, n = [], 0
    for f in files:
        rule, num, at = f.stem.split('_')
        t, letters = measure(f)
        n += 1
        if t is None: bad.append(f"{f.name}: not the video's picture"); continue
        # (an MPEG-TS stream's times begin a little after zero: the HLS clip's pictures are up to 1.4 s behind its clock)
        if at != 'x' and not (float(at) - 1.6 <= t <= float(at) + 3.5): bad.append(f"{f.name}: at {t:.1f} s"); continue
        if letters >= 0 and letters not in wanted(rule, t): bad.append(f"{f.name}: at {t:.1f} s line {letters} instead of {sorted(wanted(rule, t))[0]}")
    check(f'{run}: {what}', n > 0 and not bad, f"{n} pictures" + (f"; wrong: {bad[:5]}" if bad else ''))

def sound(run, reports, vids):
    for vid in vids:
        bad, n = [], 0
        for kind in ('tone', 'silent'):
            for e in reports.get(f's:{vid}:{kind}', []):
                n += 1
                db, pos = e['everyday']['soundLevelDb'], e['positionMs'] / 1000
                right = (pos % 20 < 10) == (kind == 'tone')    # (the place itself is in the right half of its twenty seconds)
                if not right or (db > -40) != (kind == 'tone'): bad.append(f"at {pos:.1f} s {db:.0f} dB, {kind} expected")
        check(f'{run}: {vid}: after every jump the sound is the sound of that place (tone or silence)', n >= 1 and not bad, f"{n} places" + (f"; wrong: {bad}" if bad else ''))

def requests(since=0, until=1e18):
    res = []
    f = out / 'online-requests.jsonl'
    if f.exists():
        for l in open(f):
            r = json.loads(l)
            if since <= r['t'] <= until: res.append(r)
    return res

def calls(run):
    f = out / (run + '-ytdlp.jsonl')
    return [json.loads(l) for l in open(f)] if f.exists() else []

# ---------------------------------------------------------------------------------------------- the main run
log, R = load('online')
if log is None:
    print('FAIL  no online.json'); sys.exit(1)
one = lambda label: R[label][0]
on = lambda label: R[label][0]['online']
steps_ok('online', log)
pictures('online', 'every picture is the picture of the place asked for, with the subtitle line of that place (or none)')

kinds = {'pair-h264': (2, '360p · H.264 + AAC'), 'pair-vp9': (2, '360p · VP9 + Opus'), 'mixed-a': (2, '360p · H.264 + Opus'), 'mixed-b': (2, '360p · VP9 + AAC'),
         'muxed': (1, '360p · H.264 + AAC'), 'hls': (0, '360p · H.264 + AAC'), 'big': (2, '720p · H.264 + AAC')}
for vid, (streams, what) in kinds.items():
    e = one('open:' + vid); o = e['online']
    how = {2: 'the picture and the sound as two streams', 1: 'one stream', 0: 'its manifest, played as it is'}[streams]
    check(f'{vid}: plays, {how}', e['state'] == 'playing' and o['player']['streams'] == streams and o['what'] == what and o['site'] == 'Youtube'
          and e['file'] == f'Stand-in video ({vid})' and o['page'] == f'{site}/yt/watch?v={vid}' and not e['lastError'],
          f"{o['player'].get('kinds', [])} | {o['what']} | {e['file']}")
sound('online', R, kinds.keys())
for vid in ('pair-h264', 'pair-vp9'):
    series = [R[f'b:{vid}:{i:02d}'][0] for i in range(20)]
    quiet = next((e for e in series if e['everyday']['soundLevelDb'] < -60), None)
    before = [e for e in series if e['positionMs'] < 28900]
    check(f'{vid}: the tone stops when the picture reaches 30 s (the sound is where the picture is)',
          quiet is not None and 29.0 <= quiet['positionMs'] / 1000 <= 31.2 and before and all(e['everyday']['soundLevelDb'] > -40 for e in before),
          f"silence first measured at {quiet['positionMs'] / 1000:.2f} s" if quiet else 'never silent')

pa, pb, po, fa, na = one('paused-a'), one('paused-b'), one('played-on'), one('fast'), one('normal-again')
check('paused: the picture stays', pa['state'] == 'paused' and pa['framePtsMs'] == pb['framePtsMs'], f"{pa['framePtsMs']:.0f} ms, {pb['framePtsMs']:.0f} ms")
check('played on: it goes on from there', 1200 <= po['positionMs'] - pb['positionMs'] <= 3200, f"{po['positionMs'] - pb['positionMs']:.0f} ms in 2 s")
check('twice the speed: twice the video in the time', 4500 <= fa['positionMs'] - po['positionMs'] <= 7800 and 900 <= na['positionMs'] - fa['positionMs'] <= 2600,
      f"{fa['positionMs'] - po['positionMs']:.0f} ms in 3 s; then {na['positionMs'] - fa['positionMs']:.0f} ms in 1.5 s")
st = [one(f'step-{i}')['framePtsMs'] for i in range(4)]
check('one picture forward, another, one back', abs(st[1] - st[0] - 40) < 1 and abs(st[2] - st[1] - 40) < 1 and st[1] <= st[3] < st[2], f"{[round(x) for x in st]} ms")

o = on('subs-english')
check('the subtitle files the site offers: those people wrote, then the automatic captions of the video\'s own language',
      o['subtitles'] == ['English', 'French', 'English (automatic)'] and o['subtitlesAutomatic'] == [False, False, True], f"{o['subtitles']}")
loaded = [on(l)['subtitleLoaded'] for l in ('subs-english', 'subs-automatic', 'subs-french', 'subs-off')]
check('each is loaded when picked, and put away again', loaded == ['English', 'English (automatic)', 'French', ''], f"{loaded}")
c1, c2 = one('chapter-1'), one('chapter-2')
check('chapters come from the page', len(on('chapter-1')['chapters']) == 3 and 30000 <= c1['positionMs'] <= 33000 and 70000 <= c2['positionMs'] <= 73000,
      f"{[c['title'] for c in on('chapter-1')['chapters']]}; next chapter: {c1['positionMs'] / 1000:.1f} s, {c2['positionMs'] / 1000:.1f} s")

# The large file: a jump asks for the part it needs, not for everything up to it.
jumps = []
for i in range(4):
    rq = [r for r in requests(on(f'big-{i}')['wallClock'], on(f'big-{i + 1}')['wallClock']) if r['path'].startswith('/media/v_big.mp4')]
    jumps.append((len(rq), all(r['status'] == 206 and r['range'] for r in rq)))
check('a 90 MB file: each jump (far ahead, back, ahead) takes a few requests by byte range', all(1 <= n <= 8 and ranged for n, ranged in jumps), f"requests for each jump: {[n for n, _ in jumps]}")

es, ea, eg, ed = one('expire-start'), one('expire-after'), one('expire-again'), one('expire-dead')
refused = [r for r in requests(es['online']['wallClock'], ea['online']['wallClock']) if r['status'] == 403]
check('addresses that stopped working: yt-dlp is asked again, once, and the video goes on at the place asked for',
      ea['state'] == 'playing' and ea['online']['askedAgain'] == 1 and ea['online']['resolves'] == es['online']['resolves'] + 1 and 90000 <= ea['positionMs'] <= 95000
      and not ea['lastError'] and not ea['online']['dialogs'] and len(refused) >= 1,
      f"the server refused {len(refused)} request(s); asked again {ea['online']['askedAgain']} time; playing at {ea['positionMs'] / 1000:.1f} s; dialogs: {ea['online']['dialogs']}")
check('... and again when it happens again later', eg['state'] == 'playing' and eg['online']['askedAgain'] == 2 and 40000 <= eg['positionMs'] <= 46000 and not eg['lastError'] and not eg['online']['dialogs'],
      f"asked again {eg['online']['askedAgain']} times; playing at {eg['positionMs'] / 1000:.1f} s")
check('addresses that never work: asked again once, then said, in one dialog',
      ed['state'] == 'error' and ed['online']['askedAgain'] == 3 and ed['online']['resolves'] == eg['online']['resolves'] + 2 and 'would not send' in ed['lastError'] and 'Forbidden' in ed['lastError']
      and len(ed['online']['dialogs']) == 1, f"{ed['lastError'].splitlines()[0]} | {len(ed['online']['dialogs'])} dialog")

p1, p2, p3 = on('playlist-first'), on('playlist-second'), on('playlist-third')
ref = lambda vid: f"web:{site}/yt/watch?v={vid}#Stand-in%20video%20%28{vid}%29"
check('a playlist: its videos take its place in the list, by their names', p1['playlistLabels'] == ['Web · Stand-in video (pair-h264)', 'Web · Second of three', 'Web · Third of three']
      and p1['playlistIndex'] == 0 and p1['page'].endswith('v=pair-h264') and one('playlist-first')['state'] == 'playing', f"{p1['playlistLabels']}")
check('the next one is asked for when its turn comes: by hand, and when a video ends',
      p2['playlistIndex'] == 1 and p2['page'].endswith('v=pair-vp9') and p3['playlistIndex'] == 2 and p3['page'].endswith('v=muxed') and one('playlist-third')['state'] == 'playing'
      and p3['playlist'] == [ref('pair-h264'), ref('pair-vp9'), ref('muxed')], f"{p3['playlistLabels']}")
saved = (out / 'online' / 'list.m3u8').read_text().split('\n')
check('a saved playlist holds the pages\' plain addresses (what any player reads), the names beside them',
      saved[:7] == ['#EXTM3U', '#EXTINF:-1,Web · Stand-in video (pair-h264)', f'{site}/yt/watch?v=pair-h264', '#EXTINF:-1,Web · Stand-in video (pair-vp9)',
                    f'{site}/yt/watch?v=pair-vp9', '#EXTINF:-1,Web · Stand-in video (muxed)', f'{site}/yt/watch?v=muxed'], f"{saved[1:3]}")
pf = on('playlist-file')
check('... and opens again as it was', pf['playlist'] == [ref('pair-h264'), ref('pair-vp9'), ref('muxed')] and one('playlist-file')['state'] == 'playing', f"{pf['playlistLabels']}")
sh = on('short')
check('a short address: the video is kept under its own page', sh['page'] == f'{site}/yt/watch?v=mixed-a' and sh['playlist'] == [ref('mixed-a')] and sh['resumeKey'] == f'web:{site}/yt/watch?v=mixed-a', sh['page'])
left, back = one('resume-left'), one('resume-back')
check('a web video goes on where it was left', back['state'] == 'playing' and left['positionMs'] - 500 <= back['positionMs'] <= left['positionMs'] + 4000,
      f"left at {left['positionMs'] / 1000:.1f} s, back at {back['positionMs'] / 1000:.1f} s")

g, npg, ut, un, ml = one('gone'), one('none-page'), one('untyped'), one('untyped-nothing'), one('mislabeled')
check('a video that is gone: what yt-dlp says is shown', 'This video is private' in g['lastError'] and g['lastError'].startswith('yt-dlp could not find the video') and len(g['online']['dialogs']) == 1
      and g['online']['dialogs'][0].startswith('webErrorDialog') and g['state'] != 'playing', g['lastError'])
check('a page without a video: said, and not tried as a video file', 'Unsupported URL' in npg['lastError'] and npg['state'] == 'idle' and len(npg['online']['dialogs']) == 1, npg['lastError'])
check('an address whose server does not say what it is, and yt-dlp knows nothing: tried as a stream, and it is one', ut['state'] == 'playing' and 'Unsupported URL' in ut['online']['fallbackNote'] and not ut['lastError'],
      f"{ut['state']}, picture at {ut['framePtsMs'] / 1000:.1f} s")
check('... and when it is nothing at all: both reasons are shown, in one dialog', un['state'] == 'error' and 'yt-dlp found no video at this address' in un['lastError'] and 'does not play as a file or stream either' in un['lastError']
      and len(un['online']['dialogs']) == 1, un['lastError'].splitlines()[0])
check('a video file sent as a page (yt-dlp sees that it is one)', ml['state'] == 'playing' and ml['online']['player']['streams'] == 1, f"{ml['state']}, {ml['file']}")

nj, nj2 = one('nojs'), one('nojs-again')
check('yt-dlp found no JavaScript runtime: said once, the video plays', nj['state'] == 'playing' and len(nj['online']['notes']) == 1 and 'JavaScript' in nj['lastWarning'] and len(nj2['online']['notes']) == 1,
      nj['online']['notes'][0][:70] + '…')
h = one('headers')
rq = [r for r in requests(nj2['online']['wallClock'], h['online']['wallClock']) if r['path'].startswith('/media/')]
want = {'ua': 'Mozilla/5.0 (X11; Linux x86_64) stand-in/1.0', 'referer': f'{site}/yt/', 'x': 'sent-along'}
streams = [r for r in rq if r['path'].split('?')[0] in ('/media/v_h264.mp4', '/media/a_aac.m4a')]
subs = [r for r in rq if r['path'].startswith('/media/en.vtt')]
hdr_ok = lambda r: all(r[k] == v for k, v in want.items()) and sorted((r['cookie'] or '').split('; ')) == ['pref=wide', 'session=abc123']
check('what the site wants sent along goes with every request: who is asking, where from, its own header, cookies',
      len(streams) >= 2 and all(hdr_ok(r) for r in streams) and subs and all(hdr_ok(r) for r in subs),
      f"{len(streams)} stream requests, {len(subs)} for the subtitle file; e.g. {streams[0]['cookie'] if streams else '-'} | {streams[0]['ua'] if streams else '-'}")
lv = one('live')
check('a live stream plays, and is not something to come back to', lv['state'] == 'playing' and lv['online']['live'] and lv['online']['resumeKey'] == '' and lv['online']['player']['streams'] == 0,
      f"live: {lv['online']['live']}, kept under: '{lv['online']['resumeKey']}'")
sa, sc = one('slow-asking'), one('slow-cancelled')
asked_slow = any(c['argv'][-1].endswith('v=slow') for c in calls('online'))
check('something else opened while yt-dlp is still thinking: its late answer is not used', asked_slow and 'finding the video' in sa['online']['hint'] and sc['state'] == 'playing'
      and sc['file'] == 'sd_4x3_h264.mp4' and sc['online']['page'] == '' and sc['positionMs'] > 3000, f"while asking: \"{sa['online']['hint'].splitlines()[0]}\"; 4.5 s later: {sc['file']} at {sc['positionMs'] / 1000:.1f} s")
ps = one('pasted')
check('an address pasted with Ctrl+V, in the middle of other text', ps['state'] == 'playing' and ps['online']['page'] == f'{site}/yt/watch?v=mixed-b', ps['online']['page'])
check('the largest picture asked for is the one set', '[height<=720]' in ps['online']['lastCommand'] and '[height<=1080]' not in ps['online']['lastCommand'], '720p at most')
first = on('open:pair-h264')
if one('open:pair-h264')['videoPath']['software']:
    check('left to the player, on this screen without a graphics card: 1080p at most, H.264 where there is a choice',
          first['maxHeight'] == 1080 and first['preferH264'] and '[height<=1080]' in first['lastCommand'] and '-S res,vcodec:h264' in first['lastCommand'], f"{first['maxHeight']}p")
all_calls = [c for c in calls('online') if '--version' not in c['argv']]
check('yt-dlp is always asked the same way: one answer, nothing downloaded, the address after "--", its JavaScript runtime named',
      len(all_calls) >= 25 and all(c['argv'][0] == '--dump-single-json' and c['argv'][-2] == '--' and '--no-playlist' in c['argv'] for c in all_calls)
      and all('--js-runtimes' in c['argv'] and c['argv'][c['argv'].index('--js-runtimes') + 1].startswith('node:') for c in all_calls if '/old/' not in c['self']),
      f"{len(all_calls)} calls")
old, og = one('old'), one('old-gone')
old_calls = [c for c in all_calls if '/old/' in c['self'] and c['argv'][-1].endswith('v=pair-h264')]
check('an older yt-dlp that does not know --js-runtimes: asked once more without it', old['state'] == 'playing' and len(old_calls) == 2 and '--js-runtimes' in old_calls[0]['argv']
      and '--js-runtimes' not in old_calls[1]['argv'] and old['online']['version'] == '2024.08.06', f"{len(old_calls)} calls; yt-dlp {old['online']['version']}")
check('... and when it fails, its age is said', 'This yt-dlp is from 2024.08.06' in og['lastError'] and 'days old' in old['online']['status'], og['lastError'].split('\n\n')[-1][:80] + '…')
sb = one('subs-by-itself')
check('subtitles left on: a web video\'s subtitle file loads by itself, the one people wrote', sb['online']['subtitleLoaded'] == 'English' and sb['everyday']['subtitlesWanted'], sb['online']['subtitleLoaded'])
end = one('end')
web = [r for r in end['recentFiles'] if r.startswith('web:')]
check('web videos are in "Recent" by their names', len(web) >= 8 and 'web:Stand-in video (pair-h264)' in web and len(set(web)) == len(web), f"{len(web)} of {len(end['recentFiles'])}: {web[:3]}")
check('the settings say which yt-dlp there is', end['online']['status'].startswith('yt-dlp 2026.09.30 (') and 'JavaScript for YouTube: Node.' in end['online']['status']
      and end['online']['button'] == 'Use the newest yt-dlp instead', end['online']['status'][:20] + '… ' + end['online']['status'][-30:])
others = [r['path'] for r in requests() if not re.match(r'/(media|watch|yt|release|stream|mislabeled|untyped)', r['path'])]
check('nothing else was asked of the site', not others, f"{len(requests())} requests in all")

# ---------------------------------------------------------------------------------------------- the second start
log2, R2 = load('online-restore')
if log2 is not None:
    steps_ok('online-restore', log2)
    pictures('online-restore', 'after a restart the video goes on where it was left')
    rs, rr = R2['restore-start'][0], R2['restore'][0]
    check('after a restart: "Recent" still has the web videos, and the one left goes on from its place',
          any(r.startswith('web:') for r in rs['recentFiles']) and rr['state'] == 'playing' and left['positionMs'] - 500 <= rr['positionMs'] <= left['positionMs'] + 4000,
          f"at {rr['positionMs'] / 1000:.1f} s")

# ---------------------------------------------------------------------------------------------- no yt-dlp
log3, R3 = load('online-none')
if log3 is not None:
    steps_ok('online-none', log3)
    pictures('online-none', 'the pictures are the pictures of the places asked for')
    n0, dt, df, dh, ak, ft = (R3[l][0] for l in ('none', 'direct-type', 'direct-file', 'direct-hls', 'asked', 'fetched'))
    check('no yt-dlp: the settings say so and offer to fetch it', n0['online']['program'] == '' and 'not on this computer' in n0['online']['status'] and n0['online']['button'] == 'Get yt-dlp', n0['online']['status'][:60] + '…')
    check('an address that is a video (its server says so, its name does not) plays as before, without yt-dlp',
          dt['state'] == 'playing' and dt['file'] == 'stream' and not dt['online']['dialogs'] and dt['online']['asked'] == '', f"{dt['file']} at {dt['positionMs'] / 1000:.1f} s")
    check('a video file\'s address, and an HLS stream\'s, play as before', df['state'] == 'playing' and df['file'] == 'muxed.mp4' and dh['state'] == 'playing' and dh['file'] == 'master.m3u8',
          f"{df['file']}, {dh['file']}")
    check('a page: the player says what is needed and offers to fetch it', ak['online']['asked'] == 'no-program' and len(ak['online']['dialogs']) == 1 and ak['online']['dialogs'][0].startswith('ytDlpDialog')
          and 'yt-dlp is needed' in ak['online']['hint'] and ak['state'] == 'idle', ak['online']['dialogs'][0][:70] + '…')
    got = [r['path'] for r in requests(ak['online']['wallClock'], ft['online']['wallClock']) if r['path'].startswith('/release/')]
    check('"Get yt-dlp": the release\'s checksums, then the program; Deno\'s checksum, then its archive', [g.split('/release/')[1] for g in got][:1] == ['yt-dlp/SHA2-256SUMS'] and len(got) == 4
          and got[1].startswith('/release/yt-dlp/yt-dlp') and got[2].endswith('.zip.sha256sum') and got[3].endswith('.zip'), f"{[g.split('/')[-1] for g in got]}")
    check('... both are kept in the player\'s own folder and used', ft['online']['ownCopy'] and ft['online']['program'].endswith('/CRTPlayer/tools/yt-dlp') and ft['online']['jsRuntime'].startswith('deno:')
          and ft['online']['jsRuntime'].endswith('/CRTPlayer/tools/deno') and '--js-runtimes deno:' in ft['online']['lastCommand'] and ft['online']['button'] == 'Update yt-dlp'
          and "the player's own copy" in ft['online']['status'], ft['online']['status'])
    check('... and the video that was waiting starts by itself', ft['state'] == 'playing' and ft['online']['page'].endswith('v=pair-h264') and ft['online']['player']['streams'] == 2 and not ft['online']['dialogs'],
          f"{ft['file']} at {ft['positionMs'] / 1000:.1f} s")
log4, R4 = load('online-badsum')
if log4 is not None:
    steps_ok('online-badsum', log4)
    b = R4['badsum'][0]
    check('a download that does not match its release\'s checksum is not kept, and it is said', b['online']['program'] == '' and b['online']['fetchFailures'] == 1 and b['online']['fetches'] == 0
          and any('does not match' in d for d in b['online']['dialogs']) and b['online']['button'] == 'Get yt-dlp', b['lastWarning'])
log5, R5 = load('online-baddeno')
if log5 is not None:
    steps_ok('online-baddeno', log5)
    b, bp = R5['baddeno'][0], R5['baddeno-plays'][0]
    check('yt-dlp arrives and Deno does not: yt-dlp is kept and used, what is missing is said, and the button offers it again',
          b['online']['ownCopy'] and b['online']['jsRuntime'] == '' and 'Deno' in b['lastWarning'] and 'could not be fetched' in b['lastWarning'] and b['online']['button'] == 'Update yt-dlp and get Deno'
          and bp['state'] == 'playing' and '--js-runtimes' not in bp['online']['lastCommand'], b['lastWarning'])

log7, R7 = load('online-late')
if log7 is not None:
    steps_ok('online-late', log7)
    lates = [R7[l][0] for l in ('late:big', 'late:pair-h264', 'late:pair-vp9', 'late:muxed', 'late:mixed-a')]
    check('the decoders connected a moment late, the server\'s first bytes long there: every video still opens (and jumps)',
          all(e['state'] == 'playing' and not e['lastError'] and e['online']['askedAgain'] == 0 for e in lates) and 47000 <= lates[-1]['positionMs'] <= 52000
          and lates[-1]['everyday']['soundLevelDb'] > -40, f"{[e['state'] for e in lates]}; after a jump at {lates[-1]['positionMs'] / 1000:.1f} s, {lates[-1]['everyday']['soundLevelDb']:.0f} dB")

# ---------------------------------------------------------------------------------------------- the real yt-dlp
log6, R6 = load('online-real')
if log6 is None:
    print('SKIP  the real yt-dlp against the mock site: there is none on this computer')
else:
    steps_ok('online-real', log6)
    pictures('online-real', 'the real yt-dlp: every picture is the picture of the place asked for')
    d, m, hl, npg, dr = (R6[l][0] for l in ('open:dash', 'open:muxed', 'open:hls', 'none-page', 'direct'))
    check(f"the real yt-dlp ({d['online']['version']}) takes the player's questions, with its JavaScript runtime named", re.match(r'20\d\d\.\d\d\.\d\d', d['online']['version']) is not None
          and '--js-runtimes' in d['online']['lastCommand'], d['online']['lastCommand'].split(' -f ')[0])
    check('a page with a DASH manifest naming two files: yt-dlp gives two addresses, played as the picture and the sound', d['state'] == 'playing' and d['online']['player']['streams'] == 2
          and d['online']['what'] == '360p · H.264 + AAC' and d['file'] == 'Mock video: dash (1)', f"{d['file']} | {d['online']['what']} | {d['online']['player'].get('kinds')}")
    sound('online-real', R6, ['dash', 'muxed', 'hls'])
    check('a page with an HTML5 video: one stream', m['state'] == 'playing' and m['online']['player']['streams'] == 1, m['file'])
    check('a page with an HLS stream: its manifest', hl['state'] == 'playing' and hl['online']['player']['streams'] == 0 and hl['online']['page'].endswith('/watch/hls'), hl['file'])
    check('a page without a video: yt-dlp\'s own words', 'Unsupported URL' in npg['lastError'] and npg['state'] == 'idle', npg['lastError'].splitlines()[0])
    check('an address that is a video is not put to yt-dlp at all', dr['state'] == 'playing' and dr['file'] == 'stream' and dr['online']['runs'] == npg['online']['runs'], f"{dr['online']['runs']} runs of yt-dlp in all")

print(f"\n{fails} online check(s) failed" if fails else "\nAll online checks passed")
sys.exit(1 if fails else 0)
