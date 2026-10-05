#!/usr/bin/env python3
"""Checks the subtitle runs (tests/automation/subs-*.txt): the right line is in the picture after every
jump. Usage: check-subtitles.py OUT_DIR [RUN ...]   (RUN: jump-TAG, tracks-TAG, files, jellyfin-...)

The test videos (jump_*, scripts/make-test-media.sh) say in every picture where in the video it is: a
white bar along the top grows 5 pixels a second. Their subtitle lines can be counted: line N is N letters
"O" set wide apart. So each screenshot is judged by itself: the place read off the bar, the line read
off the picture, and the two compared with the subtitle file."""
import json, re, sys, pathlib
import numpy as np
from PIL import Image

LINES = {'a': [(2, 6, 1), (10, 20, 2), (25, 28, 3), (30, 45, 4), (50, 55, 5), (60, 110, 6), (112, 118, 7)],
         'b': [(0, 58, 8), (62, 119, 9)], 'n': []}
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

def measure(path):
    """(seconds into the video or None, letters counted)"""
    a = np.asarray(Image.open(path).convert('RGB'), dtype=int)
    h, w, _ = a.shape
    s = w / 640.0
    plain = a[int(100 * s):int(200 * s)].reshape(-1, 3).mean(axis=0)
    if np.abs(plain - np.array([0x20, 0x38, 0x60])).max() > 40: return None, -1   # not the test video's picture
    white = a[int(6 * s)].min(axis=1) > 180
    n = 0
    while n < w and white[n]: n += 1
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
    """the lines that may be in a picture of time t (two at the very edge of a line)"""
    m = re.fullmatch(r'([abn])([+-]\d+)?', rule)
    delay = float(m.group(2) or 0)
    res = set()
    for dt in (-0.25, 0, 0.25):
        res.add(next((n for a, b, n in LINES[m.group(1)] if a + delay <= t + dt < b + delay), 0))
    return res

def shots(run, early=0.8):
    res = {}
    for f in sorted((out / run).glob('*.png')):
        rule, num, at = f.stem.split('_')
        if rule == 'p': continue   # (picture subtitles: judged in their own way)
        t, n = measure(f)
        if t is None: res[int(num)] = (f.name, False, 'not the video\'s picture'); continue
        want = wanted(rule, t)
        ok = n in want
        place = at == 'x' or (float(at) - early <= t <= float(at) + 3.0)
        what = f"at {t:.1f} s line {n}" + ('' if ok else f" instead of {sorted(want)[0]}") + ('' if place else f", meant to be at {at} s")
        res[int(num)] = (f.name, ok and place, what)
    return res

def group(run, res, label, numbers):
    part = [res.get(i, (f'picture {i}', False, 'missing')) for i in numbers]
    bad = [f"{name}: {what}" for name, ok, what in part if not ok]
    check(f"{run}: {label}", not bad, '; '.join(bad) if bad else ', '.join(what for _, _, what in part))

def reports(run):
    return {e['label']: e for e in json.load(open(out / f'subs-{run}.json')) if e['cmd'] == 'report'}

def opened(run, rep, times, why):
    n = rep['subtitleFeed']['videoOpens']
    check(f"{run}: the video is never opened again on the way ({why})", n == times, f"opened {n:.0f} time(s) in this run" + ('' if n == times else f", should be {times}"))

def feed_checks(run, rep, text=True, strict=True):
    f = rep['subtitleFeed']
    if text: check(f"{run}: the lines come from the player's own store", f['on'] and f['kind'] == 'text' and f['lines'] > 0, f"{f['kind']}, {f['lines']} lines ({f['caps']})")
    # (strict: the lines are all known, no wait may run out. Otherwise, as after another track was picked far into the
    # video, the reader may have to start over, and a wait may run its full quarter of a second: the picture then
    # comes without its line and is fetched again with it, which the picture checks above cover.)
    check(f"{run}: no picture waited for its lines longer than a quarter of a second", f['holdMsMax'] < 270 and (f['holdTimeouts'] == 0 or not strict),
          f"{f['holds']:.0f} waits, {f['holdMsMean']:.0f} ms on average, {f['holdMsMax']:.0f} ms the longest, {f['holdTimeouts']:.0f} given up")
    check(f"{run}: the source of the lines never failed", f['sourceErrors'] == 0 and not rep['lastError'], f"{f['sourceErrors']:.0f} errors; {rep['lastError'] or 'no error shown'}")

def jump(run):
    res = shots(run); rep = reports(run)
    group(run, res, 'the first line comes on as the video plays', [1])
    group(run, res, 'jumps while playing (forwards, back, into a line long in force, between lines, to a keyframe)', range(2, 8))
    group(run, res, 'jumps while paused: the picture that stands has its line', range(8, 15))
    group(run, res, 'played on from a paused jump: the line ends, the next one comes', [15, 16])
    # frame by frame: 9.88 s (none), four frames on (10.04 s: the line has begun), five back (9.84 s: none)
    steps = [measure(out / run / f)[1] for f in sorted(p.name for p in (out / run).glob('*.png')) if int(f.split('_')[1]) in (17, 18, 19)]
    check(f"{run}: frame by frame over the beginning of a line, and back", steps == [0, 2, 0], f"lines in the three pictures: {steps} (should be none, line 2, none)")
    group(run, res, 'hidden and shown again, paused and playing', range(20, 24))
    group(run, res, 'at double and at half speed', range(24, 28))
    group(run, res, 'round an A-B loop', [28, 29])
    group(run, res, 'dragged along the seek bar, playing and paused', [30, 31])
    stall = max((e.get('maxEventLoopStallMs', 0) for e in json.load(open(out / f'subs-{run}.json')) if e['cmd'].startswith('scrub')), default=-1)
    check(f"{run}: ... and the window stays responsive meanwhile", 0 <= stall < 1000, f"longest stall {stall:.0f} ms (the same as before the change, on this machine)")
    group(run, res, 'near the end', [32])
    opened(run, rep['end'], 1, 'hiding and showing subtitles, other speeds, the loop')
    feed_checks(run, rep['end'])

def tracks(run):
    res = shots(run); rep = reports(run)
    group(run, res, 'another track picked, paused and playing; subtitles off and on', range(1, 10))
    group(run, res, 'lines three seconds later', [10, 11])
    group(run, res, 'lines two seconds earlier, then another track with the same delay, then no delay', range(12, 17))
    opened(run, rep['end'], 1, 'other tracks, subtitles off and on, delays')
    feed_checks(run, rep['end'], strict=False)

def files(run):
    res = shots(run); rep = reports(run)
    group(run, res, 'the subtitle file beside the video', [1, 2])
    group(run, res, 'files picked while the video is open (SubRip, ASS, WebVTT), jumps after each', range(3, 8))
    group(run, res, 'the file put away', [8])
    same = rep['side-before']['subtitleFeed']['videoOpens'] == rep['side-off']['subtitleFeed']['videoOpens']
    moved = rep['side-picked']['positionMs'] - rep['side-before']['positionMs'], rep['side-picked']['t'] - rep['side-before']['t']
    check(f"{run}: picking a file leaves the video playing (it is not opened again)", same and abs(moved[0] - moved[1]) < 700,
          f"opened {rep['side-off']['subtitleFeed']['videoOpens']:.0f} time(s); {moved[0]:.0f} ms of video in {moved[1]:.0f} ms")
    group(run, res, 'a file picked for a video with subtitle tracks of its own; back to its own track; the file again; put away', range(9, 16))
    group(run, res, 'a video that was left in the middle starts there with its line', [16, 17])
    back = [measure(out / run / f)[0] for f in ('a_16_x.png', 'a_17_x.png')]
    check(f"{run}: ... (it did start where it was left, at 40 s)", all(t is not None and 39 <= t <= 55 for t in back), ', '.join(f"{t:.1f} s" for t in back if t is not None))
    check(f"{run}: the picked file is listed after the video's own tracks", len(rep['own-picked']['subtitleTracks']) == 3 and rep['own-picked']['currentSubtitle'] == 2,
          f"{len(rep['own-picked']['subtitleTracks'])} tracks, number {rep['own-picked']['currentSubtitle']} showing")
    feed_checks(run, rep['end'])

def pictures(run):
    res = shots(run); rep = reports(run)
    f = rep['pictures']['subtitleFeed']
    def seen(numbers):
        return [measure(out / run / p.name) for p in sorted((out / run).glob('*.png')) if int(p.stem.split('_')[1]) in numbers]
    def turn(label, numbers, line):
        got = seen(numbers)
        check(f"{run}: {label}", any(n == line for _, n in got) and all(n in (0, line, line - 1, line + 1) for _, n in got),
              ', '.join(f"{t:.1f} s: {n}" for t, n in got))
    turn('a picture-subtitle track is drawn as the video plays (line 2 comes up)', range(1, 9), 2)
    check(f"{run}: picture subtitles are left to the playback library", f['kind'] == 'unsupported' and f['handedOver'] == 0, f"{f['kind']} ({f['caps']})")
    turn('... and after a jump, from the next line on (line 3)', range(9, 21), 3)
    group(run, res, 'the text track of the same video: the line is there right after a jump', [21, 22])
    t = rep['text']['subtitleFeed']
    check(f"{run}: ... from the player's own store", t['kind'] == 'text' and t['lines'] == 7, f"{t['kind']}, {t['lines']} lines")
    turn('back to the picture track (line 5 comes up)', range(23, 37), 5)
    off = seen(range(37, 45))
    check(f"{run}: subtitles off: no picture subtitle either", len(off) == 8 and all(n == 0 for _, n in off), ', '.join(f"{t:.1f} s: {n}" for t, n in off))
    check(f"{run}: no error on the way", not rep['end']['lastError'] and rep['end']['subtitleFeed']['sourceErrors'] == 0, rep['end']['lastError'] or 'none')
    n = rep['end']['subtitleFeed']['videoOpens']
    check(f"{run}: the video is opened again once, for its picture subtitles, and not again after that", n == 2 and rep['pictures']['subtitleFeed']['videoOpens'] == 2,
          f"opened {n:.0f} times in this run")

def jellyfin(run):
    res = shots(run); rep = reports(run)
    group(run, res, 'the original file, its own tracks: jumps playing and paused, the other track', range(1, 9))
    own = rep['jf-own']
    reqs = [json.loads(l) for l in open(out / 'requests.jsonl')]
    files = [r for r in reqs if re.fullmatch(r'/Videos/j1/j1/Subtitles/[23]/0/Stream\.srt', r['path'])]
    check(f"{run}: the original file is played as it is; the lines of its own tracks come from the server as files, signed in (token in the header only)",
          own['jellyfinPlayMethod'] == 'DirectPlay' and own['subtitleFeed']['kind'] == 'text' and own['subtitleFeed']['textIndex'] == -1 and
          sorted(set(r['path'] for r in files)) == ['/Videos/j1/j1/Subtitles/2/0/Stream.srt', '/Videos/j1/j1/Subtitles/3/0/Stream.srt'] and
          all(r['authHeader'] and not r['tokenInUrl'] and r['status'] == 200 for r in files),
          f"{own['jellyfinPlayMethod']}, {own['subtitleFeed']['lines']} lines in the player's store, {len(files)} subtitle file request(s)")
    group(run, res, 'subtitle files from the server (SubRip by itself, then ASS and WebVTT picked): jumps playing and paused', range(9, 16))
    same = rep['jf-files-before']['subtitleFeed']['videoOpens'] == rep['jf-files']['subtitleFeed']['videoOpens']
    check(f"{run}: picking another of the server's files leaves the video as it is", same, 'not opened again' if same else 'opened again')
    # (a conversion's picture may lag the place it is said to be at: MPEG-TS segments begin a little after their clock)
    group(run, shots(run, early=1.3), 'converted by the server: jumps playing and paused, the other track\'s file', range(16, 23))
    conv = rep['jf-converted']
    same = rep['jf-converted-before']['subtitleFeed']['videoOpens'] == conv['subtitleFeed']['videoOpens']
    check(f"{run}: it is a conversion, and picking the other file does not start it anew", conv['jellyfinPlayMethod'] == 'Transcode' and same,
          f"{conv['jellyfinPlayMethod']}; " + ('not opened again' if same else 'opened again'))
    opens = [rep[k]['subtitleFeed']['videoOpens'] for k in ('jf-own', 'jf-files', 'jf-converted')]
    check(f"{run}: each of the three videos is opened once", opens == [1, 2, 3], f"opened {opens[-1]:.0f} times in all")
    for label in ('jf-own', 'jf-files', 'jf-converted'): feed_checks(f"{run} ({label[3:]})", rep[label])

for run in sys.argv[2:]:
    kind = run.split('-')[0]
    try:
        {'jump': jump, 'tracks': tracks, 'files': files, 'pictures': pictures, 'jellyfin': jellyfin}[kind](run)
    except Exception as e:   # (a run that did not finish)
        check(f"{run}: the run can be read", False, repr(e))
sys.exit(1 if fails else 0)
