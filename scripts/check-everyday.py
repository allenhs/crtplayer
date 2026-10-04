#!/usr/bin/env python3
"""Checks the 2.11 everyday-playback run (tests/automation/everyday.txt and everyday-restore.txt).
Usage: check-everyday.py OUT_DIR"""
import json, os, sys
import numpy as np
from PIL import Image

out = os.path.abspath(sys.argv[1])
fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

run1 = json.load(open(os.path.join(out, 'everyday.json')))
run2 = json.load(open(os.path.join(out, 'everyday-restore.json')))
rep = {e['label']: e for e in run1 + run2 if e['cmd'] == 'report'}
ev = {k: v['everyday'] for k, v in rep.items()}
bad = [e['cmd'] for e in run1 + run2 if e.get('ok') is False]
check('both scripts ran without a failed step', not bad, ', '.join(bad) or 'all ok')

def img(name, mode='L'):
    return np.asarray(Image.open(os.path.join(out, name)).convert(mode)).astype(float)
def band(a, b, y0, y1):
    """Mean change between two frames in a horizontal band (fractions of the height)."""
    h = a.shape[0]
    return float(np.abs(a[int(h * y0):int(h * y1)] - b[int(h * y0):int(h * y1)]).mean())
def text_rows(a, ref):
    """Rows (as fractions of the height) where frame a differs from the text-free frame ref."""
    d = np.abs(a - ref)
    if d.ndim == 3: d = d.max(axis=2)
    rows = np.where((d > 40).sum(axis=1) >= 3)[0]
    return (rows.min() / a.shape[0], rows.max() / a.shape[0], int((d > 40).sum())) if len(rows) else (None, None, 0)

# ---- subtitles: off by default, then as set
d0, d1 = img('ev-sub-default.png'), img('ev-sub-on.png')
check('subtitles are off by default (a video with two subtitle tracks)', rep['sub-default']['currentSubtitle'] == -1 and not ev['sub-default']['subtitlesWanted']
      and len(rep['sub-default']['subtitleTracks']) == 2, f"track {rep['sub-default']['currentSubtitle']} of {len(rep['sub-default']['subtitleTracks'])}")
check('V turns them on: the text is drawn into the picture', rep['sub-on']['currentSubtitle'] == 0 and band(d1, d0, 0.7, 1.0) > 3 * max(0.2, band(d1, d0, 0.0, 0.5)),
      f"track {rep['sub-on']['currentSubtitle']}; bottom of the picture changed by {band(d1, d0, 0.7, 1.0):.2f}, the top half by {band(d1, d0, 0.0, 0.5):.2f}")
check('picking a track remembers its language', ev['sub-fr']['subtitleLang'] == 'fr' and ev['picked']['subtitleLang'] == 'en' and ev['picked']['audioLang'] == 'ja',
      f"after French: {ev['sub-fr']['subtitleLang']}; then English subtitles, Japanese sound: {ev['picked']['subtitleLang']} / {ev['picked']['audioLang']}")
hj, ps = ev['heard-ja'], ev['paused-switch']
check('the sound heard is the track picked (its 880 Hz tone, not the other track\'s 440 Hz)', abs(hj['soundPitchHz'] - 880) < 30, f"{hj['soundPitchHz']:.0f} Hz")
check('changing the sound track while paused, then seeking, no longer freezes the player', rep['paused-switch']['state'] == 'playing' and rep['paused-switch']['positionMs'] > 7500
      and rep['paused-switch']['currentAudio'] == 1 and abs(ps['soundPitchHz'] - 880) < 30,
      f"{rep['paused-switch']['state']} at {rep['paused-switch']['positionMs'] / 1000:.1f} s, track {rep['paused-switch']['currentAudio'] + 1}, {ps['soundPitchHz']:.0f} Hz")
o = rep['other-order']
check('the next video: the same languages, wherever they are in the file', ev['other-order']['currentSubtitleLang'] == 'en' and o['currentSubtitle'] == 1
      and ev['other-order']['currentAudioLang'] == 'ja' and o['currentAudio'] == 0 and ev['other-order']['subtitleLangs'] == ['fr', 'en']
      and abs(ev['other-order']['soundPitchHz'] - 880) < 30,
      f"subtitles: track {o['currentSubtitle'] + 1} ({ev['other-order']['currentSubtitleLang']}) of {ev['other-order']['subtitleLangs']}; "
      f"sound: track {o['currentAudio'] + 1} ({ev['other-order']['currentAudioLang']}) of {ev['other-order']['audioLangs']}")
check('a video without subtitles in between changes nothing', rep['no-subs']['currentSubtitle'] in (-1, 0) and ev['no-subs']['subtitlesWanted']
      and ev['back']['currentSubtitleLang'] == 'en' and ev['back']['currentAudioLang'] == 'ja' and rep['back']['currentAudio'] == 1
      and abs(ev['back']['soundPitchHz'] - 880) < 30,
      f"back at the first video: subtitles {ev['back']['currentSubtitleLang']}, sound track {rep['back']['currentAudio'] + 1} ({ev['back']['currentAudioLang']}, {ev['back']['soundPitchHz']:.0f} Hz heard)")
check('turned off, they stay off in the next video', rep['off-carried']['currentSubtitle'] == -1 and not ev['off-carried']['subtitlesWanted']
      and ev['off-carried']['currentAudioLang'] == 'ja', f"track {rep['off-carried']['currentSubtitle']}; the sound is still {ev['off-carried']['currentAudioLang']}")
s0, s1 = img('ev-sidecar-off.png'), img('ev-sidecar-on.png')
check('a subtitle file next to the video is not shown while subtitles are off, and is there as soon as they are on',
      rep['sidecar-off']['currentSubtitle'] == -1 and rep['sidecar-off']['externalSubtitle'] == 'subs_clip.srt'
      and rep['sidecar-on']['currentSubtitle'] >= 0 and band(s1, s0, 0.7, 1.0) > 3 * max(0.2, band(s1, s0, 0.0, 0.5)),
      f"{rep['sidecar-off']['externalSubtitle']}: off, then on (bottom of the picture changed by {band(s1, s0, 0.7, 1.0):.2f})")

# ---- subtitle delay: the line is on screen from 4 to 6 seconds
t3, early, late_a, late_b, norm = (img(n) for n in ('ev-timed-3.png', 'ev-timed-early.png', 'ev-timed-late-a.png', 'ev-timed-late-b.png', 'ev-style-normal.png'))
has = lambda a: text_rows(a, t3)[2] > 200
pe, pa, pb = (rep[k]['positionMs'] / 1000 for k in ('sub-early', 'sub-late-a', 'sub-late-b'))
check('subtitle delay −1.5 s: the line is on screen before its stored time', not has(t3) and has(early) and 2.6 < pe < 3.9 and ev['sub-early']['textOffsetMs'] == -1500,
      f"at {pe:.1f} s the line shows (stored from 4 s; at 3 s without the delay: {'shown' if has(t3) else 'not shown'})")
check('subtitle delay +1.5 s: the line comes later and stays later', not has(late_a) and 4.1 < pa < 5.4 and has(late_b) and 6.1 < pb < 7.4 and has(norm),
      f"at {pa:.1f} s not yet shown (without the delay it is, at 4.5 s); at {pb:.1f} s still shown (stored until 6 s)")
# ---- subtitle style
large, small, yellow, box, top, raised = (img(f'ev-style-{n}.png') for n in ('large', 'small', 'yellow', 'box', 'top', 'raised'))
def height(a):
    y0, y1, _ = text_rows(a, t3)
    return (y1 - y0) if y0 is not None else 0
hs, hn, hl = height(small), height(norm), height(large)
check('size: small, normal, very large', 0 < hs < hn < hl and hl > 1.5 * hs, f"the text is {hs * 100:.1f}%, {hn * 100:.1f}% and {hl * 100:.1f}% of the picture's height")
yel = img('ev-style-yellow.png', 'RGB'); whi = img('ev-style-normal.png', 'RGB')
def yellow_share(a):
    m = (a[..., 0] > 200) & (a[..., 1] > 190)
    return float(((a[..., 2] < 140) & m).sum()) / max(1, int(m.sum()))
check('colour: yellow', yellow_share(yel) > 0.6 and yellow_share(whi) < 0.1, f"{yellow_share(yel) * 100:.0f}% of the bright text pixels are yellow ({yellow_share(whi) * 100:.0f}% when white)")
# the box: the space around the letters, inside the text's own rectangle, turns dark; the outline leaves the picture there
def dark_inside(a):
    d = np.abs(a - t3)
    ys, xs = np.where(d > 40)
    region = (slice(ys.min(), ys.max() + 1), slice(xs.min(), xs.max() + 1))
    return float((a[region] < t3[region] - 30).mean())
check('behind the text: a dark box', dark_inside(box) > 0.45 and dark_inside(norm) < 0.6 * dark_inside(box),
      f"{dark_inside(box) * 100:.0f}% of the text's rectangle is dark with the box, {dark_inside(norm) * 100:.0f}% with the outline")
yn, yr, yt = text_rows(norm, t3)[0], text_rows(raised, t3)[0], text_rows(top, t3)[0]
check('position: bottom, raised, top', yt is not None and yt < 0.2 and yn > 0.75 and yt < yr < yn - 0.04, f"the text starts {yn * 100:.0f}%, {yr * 100:.0f}% and {yt * 100:.0f}% of the way down")

# ---- sound delay
later, earlier, base = ev['adelay-later'], ev['adelay-earlier'], rep['sync-base']
check('sound delay +400 ms: the sound is held back', later['audioSinkOffsetMs'] == 400 and later['videoSinkOffsetMs'] == 0, f"sound output delayed {later['audioSinkOffsetMs']} ms, picture {later['videoSinkOffsetMs']} ms")
shift = rep['adelay-earlier']['sync']['meanMs'] - base['sync']['meanMs']
check('sound delay −400 ms: the picture is held back instead (measured on the frames)', earlier['videoSinkOffsetMs'] == 400 and earlier['audioSinkOffsetMs'] == 0 and 250 < shift < 550,
      f"picture output delayed {earlier['videoSinkOffsetMs']} ms; frames arrive {shift:.0f} ms later against the sound's clock than before")
check('the sound delay carries over to the next video; a subtitle delay does not', ev['deint-on']['audioDelayMs'] == 100 and ev['subdelay-set']['subtitleDelayMs'] == 700
      and ev['sync-base']['subtitleDelayMs'] == 0, f"sound {ev['deint-on']['audioDelayMs']} ms; subtitles {ev['subdelay-set']['subtitleDelayMs']} ms -> {ev['sync-base']['subtitleDelayMs']} ms in the next video")

# ---- deinterlacing
def combing(a):
    """How much stronger the difference between neighbouring rows is than between rows two apart
    (interlaced fields of a moving object alternate row by row)."""
    one = np.abs(a[1:-1] - a[:-2]).mean()
    two = np.abs(a[2:] - a[:-2]).mean()
    return float(one - two)
con, coff = combing(img('ev-deint-on.png')), combing(img('ev-deint-off.png'))
check('interlaced video is deinterlaced', ev['deint-on']['deinterlacing'] and con < 0.25 * coff and coff > 1.0, f"combing {con:.2f} deinterlaced, {coff:.2f} as stored")
check('switched off, the frames are shown as stored', not ev['deint-off']['deinterlacing'] and not ev['deint-off']['deinterlace'], f"deinterlacing: {ev['deint-off']['deinterlacing']}")
check('progressive video is never touched', ev['progressive']['deinterlace'] and not ev['progressive']['deinterlacing'], f"setting on, deinterlacing: {ev['progressive']['deinterlacing']}")

# ---- night mode (levels measured on the sound leaving the player's own sound chain)
q, qn, ln, l = (ev[k]['soundLevelDb'] for k in ('quiet', 'quiet-night', 'loud-night', 'loud'))
pos_ok = rep['quiet']['positionMs'] < 5500 and rep['quiet-night']['positionMs'] < 5800 and rep['loud-night']['positionMs'] > 6500 and 6500 < rep['loud']['positionMs'] < 11500
check('night mode lifts the quiet part and holds down the loud part', pos_ok and qn - q > 8 and ln - l < -2 and (ln - qn) < 0.6 * (l - q),
      f"quiet {q:.1f} -> {qn:.1f} dB, loud {l:.1f} -> {ln:.1f} dB: {ln - qn:.1f} dB apart instead of {l - q:.1f}")

# ---- shuffle and repeat
five = [f'short_{c}.mp4' for c in 'abcde']
p0, p1 = len(ev['shuffle-start']['played']), ev['shuffle-end']['played']
rnd = p1[p0 - 1:]
check('shuffle: every video once, then the end', sorted(rnd) == five and rep['shuffle-end']['state'] == 'paused', ' → '.join(n[6] for n in rnd) + f"; then {rep['shuffle-end']['state']}")
ra = ev['repeat-all']['played'][len(p1):]
check('repeat the playlist: after the last video, the first', ra[:2] == ['short_e.mp4', 'short_a.mp4'], ' → '.join(n[6] for n in ra))
ro = ev['repeat-one']['played'][len(ev['repeat-all']['played']):]
cur = ev['repeat-all']['played'][-1]
check('repeat this video: the same one again', len(ro) >= 1 and all(n == cur for n in ro) and rep['repeat-one']['state'] == 'playing', f"{cur[6]} → " + ' → '.join(n[6] for n in ro))

# ---- playlist files
m3u = open(os.path.join(out, 'everyday-media', 'five.m3u8'), encoding='utf-8').read()
check('the playlist saves as an .m3u8 file and opens again', m3u.startswith('#EXTM3U') and ev['m3u-loaded']['playlist'] == five and rep['m3u-loaded']['file'] == 'short_a.mp4',
      f"{len(ev['m3u-loaded']['playlist'])} entries loaded, playing {rep['m3u-loaded']['file']}")
eps = open(os.path.join(out, 'everyday-media', 'Episodes', 'episodes.m3u'), encoding='utf-8').read()
lines = [x for x in eps.splitlines() if x and not x.startswith('#')]
check('videos in the list\'s own folder are written relative to it', lines == ['Episode 1.mp4', 'Episode 2.mp4', 'Episode 10.mp4'], ', '.join(lines))

# ---- the next video in the folder
an = ev['autonext']['played'][-3:]
check('carrying on in the folder: Episode 1, 2, 10 in that order, and it stops at the last', an == ['Episode 1.mp4', 'Episode 2.mp4', 'Episode 10.mp4']
      and ev['autonext']['playlist'] == an and rep['autonext']['state'] == 'paused', ' → '.join(an) + f"; then {rep['autonext']['state']}")
off = ev['autonext-off']
check('switched off (the default), one video is one video', off['played'][-1] == 'Episode 1.mp4' and off['playlist'] == ['Episode 1.mp4'] and rep['autonext-off']['state'] == 'paused',
      f"{off['playlist']}, {rep['autonext-off']['state']}")

# ---- sleep timer
a, b, c = ev['sleep-set'], ev['sleep-fading'], ev['sleep-done']
check('sleep timer: counts down while the video plays', 8 <= a['sleepSeconds'] <= 11 and rep['sleep-set']['state'] == 'playing' and a['volume'] > 0.7,
      f"{a['sleepSeconds']} s left of 12, volume {a['volume'] * 100:.0f}%")
check('the sound fades out over the last seconds', 0 < b['sleepSeconds'] <= 4 and b['volume'] < 0.5 * a['volume'], f"{b['sleepSeconds']} s left: volume {b['volume'] * 100:.0f}%")
check('then it stops: paused, the volume back, the screen free to sleep', rep['sleep-done']['state'] == 'paused' and c['sleepCount'] == 1 and abs(c['volume'] - a['volume']) < 0.01
      and c['sleepSeconds'] == 0 and not rep['sleep-done']['sleepInhibited'],
      f"{rep['sleep-done']['state']}, volume {c['volume'] * 100:.0f}%, keeping the screen awake: {rep['sleep-done']['sleepInhibited']}")
e = ev['sleep-end']
check('"at the end of this video": it stops there instead of going on with the playlist', ev['sleep-end-set']['sleepAtEnd'] and rep['sleep-end']['state'] == 'paused'
      and e['played'][-1] == 'short_a.mp4' and e['sleepCount'] == 2 and not e['sleepAtEnd'], f"last played {e['played'][-1]} of {e['playlist']}; {rep['sleep-end']['state']}")

# ---- Cable TV
check('Cable TV: subtitles as set, from channel to channel', ev['tv-2']['currentSubtitleLang'] == 'en' and ev['tv-3']['currentSubtitleLang'] == 'en'
      and rep['tv-3']['currentSubtitle'] == 1 and rep['tv-2']['tv']['channel'] == 2 and rep['tv-3']['tv']['channel'] == 3,
      f"channel 2: track {rep['tv-2']['currentSubtitle'] + 1} ({ev['tv-2']['currentSubtitleLang']}); channel 3: track {rep['tv-3']['currentSubtitle'] + 1} ({ev['tv-3']['currentSubtitleLang']})")
check('… and the sound\'s language too', ev['tv-2']['currentAudioLang'] == 'ja' and ev['tv-3']['currentAudioLang'] == 'ja'
      and abs(ev['tv-2']['soundPitchHz'] - 880) < 30 and abs(ev['tv-3']['soundPitchHz'] - 880) < 30,
      f"channel 2: track {rep['tv-2']['currentAudio'] + 1}, channel 3: track {rep['tv-3']['currentAudio'] + 1} (both {ev['tv-3']['currentAudioLang']}; {ev['tv-2']['soundPitchHz']:.0f} and {ev['tv-3']['soundPitchHz']:.0f} Hz heard)")
check('turned off on one channel, they are off on the others; and on again', rep['tv-2-off']['currentSubtitle'] == -1 and rep['tv-3-off']['currentSubtitle'] == -1
      and rep['tv-2-off']['tv']['channel'] == 2 and rep['tv-3-off']['tv']['channel'] == 3 and ev['tv-3-on']['currentSubtitleLang'] == 'en',
      f"channel 2: {rep['tv-2-off']['currentSubtitle']}, channel 3: {rep['tv-3-off']['currentSubtitle']}; on again: {ev['tv-3-on']['currentSubtitleLang']}")
check('the sleep timer turns the TV off', not rep['tv-sleep']['tv']['on'] and rep['tv-sleep']['state'] == 'idle' and ev['tv-sleep']['sleepCount'] == 3,
      f"TV on: {rep['tv-sleep']['tv']['on']}, {rep['tv-sleep']['state']}")

e = rep['tv-sleep-end']
check('in TV mode, "at the end of this video" waits for the programme to end, then turns the TV off', ev['tv-sleep-end-set']['sleepAtEnd'] and rep['tv-sleep-end-set']['tv']['on']
      and not e['tv']['on'] and e['state'] == 'idle' and ev['tv-sleep-end']['sleepCount'] == 4,
      f"TV on: {e['tv']['on']}, {e['state']}")

# ---- the next launch
b, r = ev['before-quit'], ev['restored']
same = all(b[k] == r[k] for k in ('subtitlesWanted', 'subtitleLang', 'audioLang', 'audioDelayMs', 'subtitleStyle', 'nightMode', 'deinterlace', 'shuffle', 'repeat', 'autoNext'))
check('everything is as it was left on the next launch', same and r['subtitlesWanted'] and r['subtitleLang'] == 'en' and r['audioLang'] == 'ja' and r['audioDelayMs'] == 100
      and r['subtitleStyle'] == [2, 1, 0, 0] and r['nightMode'] and r['shuffle'] and r['repeat'] == 1 and r['autoNext'],
      f"subtitles on ({r['subtitleLang']}), sound {r['audioLang']}, sound delay {r['audioDelayMs']} ms, style {r['subtitleStyle']}, night mode, shuffle, repeat, next-in-folder")
rp = ev['restored-playing']
check('… and the first video opened gets them', rp['currentSubtitleLang'] == 'en' and rp['currentAudioLang'] == 'ja' and rp['audioSinkOffsetMs'] == 100,
      f"subtitles {rp['currentSubtitleLang']}, sound {rp['currentAudioLang']}, sound delayed {rp['audioSinkOffsetMs']} ms")
check('the sleep timer is not carried over', r['sleepSeconds'] == 0 and not r['sleepAtEnd'], 'off')
print(f"\n{fails} everyday check(s) failed" if fails else "\nAll everyday checks passed")
sys.exit(1 if fails else 0)
