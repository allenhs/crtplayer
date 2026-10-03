#!/usr/bin/env python3
"""Checks Cable TV (tests/automation/tv.txt and tv-restore.txt, run by run-verification.sh's
tv_suite against folders in OUT/tv-media and the mock Jellyfin server).
Usage: check-tv.py OUT_DIR CONFIG_HOME DATA_HOME"""
import glob, json, os, sys
from PIL import Image

out, cfg, data = sys.argv[1], sys.argv[2], sys.argv[3]
fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

run1 = json.load(open(os.path.join(out, 'tv.json')))
run2 = json.load(open(os.path.join(out, 'tv-restore.json')))
rep = {e['label']: e for e in run1 + run2 if e['cmd'] == 'report'}
bad = [e['cmd'] for e in run1 + run2 if e.get('ok') is False or 'error' in e]
check('both scripts ran without a failed step', not bad, ', '.join(bad) or 'all ok')

# ---- channels
ch = {c['number']: c for c in rep['channels']['tv']['channels']}
check('a folder becomes a channel: its videos and its subfolders\', nothing else', ch[2]['programs'] == 3 and abs(ch[2]['cycleMs'] - 65000) < 500,
      f"channel 2: {ch[2]['programs']} programmes, one round is {ch[2]['cycleMs'] / 1000:.1f} s (20 + 30 + 15)")
check('bumpers are read from their folder', ch[5]['programs'] == 3 and ch[5]['bumpers'] == 2, f"channel 5: {ch[5]['programs']} programmes, {ch[5]['bumpers']} bumpers")
check('a Jellyfin folder becomes a channel with its episodes', ch[3]['jellyfin'] and ch[3]['programs'] == 2 and abs(ch[3]['cycleMs'] - 20000) < 100,
      f"channel 3 \"{ch[3]['name']}\": {ch[3]['programs']} programmes, {ch[3]['cycleMs'] / 1000:.0f} s")
check('an empty folder is a channel with nothing on', ch[9]['programs'] == 0 and ch[9]['ready'], 'channel 9: 0 programmes')

def live(label, slack=3500):
    """Playing what the schedule says is on, about where the schedule says it is."""
    e = rep[label]; t = e['tv']
    near_edge = t['liveOffsetMs'] < 3000 or t['liveRemainingMs'] < 3000
    right = t['program'] == t['liveProgram'] or near_edge
    lag = t['scheduleOffsetMs'] - e['positionMs']
    return e['state'] == 'playing' and right and e['file'] == t['program'] and -1500 < lag < slack, \
        f"\"{t['program']}\": the broadcast is {t['scheduleOffsetMs'] / 1000:.1f} s in, the player {e['positionMs'] / 1000:.1f} s"

def in_step(label, slack=3500):
    """live(), or caught in a change-over: the programme that is on has just been asked for
    (it begins within moments, or began a few seconds ago) and is still opening."""
    ok, d = live(label, slack)
    e = rep[label]; t = e['tv']
    changing = -2000 < t['scheduleOffsetMs'] < 4500 and e['file'] == t['program'] and e['state'] in ('loading', 'paused', 'playing')
    return ok or changing, d + ('' if ok else ' (just changing over)')

def green(path, box):
    """Share of green in box (fractions of the picture): the set's chunky channel display.
    The picture is averaged in 4x4 blocks first, so the tube's phosphor mask and the fine
    grain of static come out grey and only solid lettering counts."""
    bbox, _ = picture_box(path)
    im = Image.open(os.path.join(out, path)).convert('RGB').crop(bbox)
    w, h = im.size
    c = im.crop((int(box[0] * w), int(box[1] * h), int(box[2] * w), int(box[3] * h)))
    c = c.resize((max(1, c.size[0] // 4), max(1, c.size[1] // 4)), Image.BOX)
    px = list(c.get_flattened_data() if hasattr(c, 'get_flattened_data') else c.getdata())
    return sum(1 for r, g, b in px if g > 110 and g > r + 45 and g > b + 45) / len(px)

def picture_box(path):
    """The lit area of a filtered screenshot (the picture inside the black window)."""
    im = Image.open(os.path.join(out, path)).convert('L')
    return im.point(lambda v: 255 if v > 24 else 0).getbbox(), im.size

t = rep['tuning']
check('turning the TV on tunes the first channel, with static until the picture arrives', t['tv']['on'] and t['tv']['channel'] == 2 and t['staticMoment'] == 'static',
      f"channel {t['tv']['channel']}, {t['staticMoment']}")
# (The very first tune of a first run cannot know yet how long videos take to open on this computer.)
ok, d = live('ch2', 5000)
check('it comes on partway through, where the broadcast is', ok and rep['ch2']['tv']['channel'] == 2, d)
ov = rep['badge']['tv']['overlay'].split('|')
later = rep['plain']['tv']['overlay'].split('|')
check('the channel number and what is on show for a few seconds after tuning, then go', ov[0] == 'CH 02' and ov[1] == 'MOVIES' and ov[3] != ''
      and rep['badge']['tv']['hasOverlay'] and later[0] == '',
      f"just after tuning: \"{ov[0]} {ov[1]}\", \"{ov[3]}\", \"{ov[4]}\"; later the number has gone")
ok, d = in_step('ch3', 4500)
check('channel up: the Jellyfin channel plays, in step with its schedule', ok and rep['ch3']['tv']['channel'] == 3 and rep['ch3']['source'] == 'jellyfin', d)

toons = [rep[f'toons-{i}'] for i in range(1, 6)]
# In step with the schedule - or caught in a change-over, with the next programme just opening.
changing = [-2000 < e['tv']['scheduleOffsetMs'] < 4500 and e['file'] == e['tv']['program'] for e in toons]
# (A programme up to 3 s late starts from its top rather than lose its beginning, and a bumper plays whole:
# with this machine's 2 s to open a video, the player can run up to about 5 s behind for a while.)
oks = [live(f'toons-{i}', 6500)[0] for i in range(1, 6)]
progs = [e['tv']['program'] for e in toons]
check('programmes follow one another on their own, as scheduled', all(a or b for a, b in zip(oks, changing)) and sum(oks) >= 3 and len(set(progs)) >= 2,
      ' → '.join(progs) + f" ({sum(oks)} of 5 looks in step, {5 - sum(oks)} during a change-over)")
check('bumpers play between programmes', toons[-1]['tv']['bumpersPlayed'] >= 1 and toons[-1]['tv']['programsPlayed'] >= 3,
      f"{toons[-1]['tv']['bumpersPlayed']} bumper(s), {toons[-1]['tv']['programsPlayed']} programmes played so far")

# ---- the guide: a blue grid over the lower part of the picture
def blue_share(path, lower):
    bbox, size = picture_box(path)
    im = Image.open(os.path.join(out, path)).convert('RGB')
    x0, y0, x1, y1 = bbox
    h = y1 - y0
    box = (x0 + (x1 - x0) // 3, y0 + int(h * 0.62), x1 - 8, y1 - int(h * 0.06)) if lower else (x0 + 8, y0 + int(h * 0.05), x1 - 8, y0 + int(h * 0.30))
    c = im.crop(box)
    px = list(c.get_flattened_data() if hasattr(c, 'get_flattened_data') else c.getdata())
    return sum(1 for r, g, b in px if b > r + 30 and b > g + 20 and b > 50) / len(px)
g_low, g_top, p_low = blue_share('tv-guide.png', True), blue_share('tv-guide.png', False), blue_share('tv-plain.png', True)
check('W shows the guide over the lower part of the picture, through the tube', rep['guide']['tv']['guide'] and g_low > 0.7 and g_low > p_low + 0.3,
      f'{g_low * 100:.0f}% blue in the lower part with the guide, {p_low * 100:.0f}% without; the video stays on above ({g_top * 100:.0f}% blue)')

ok, d = in_step('ch2-again')
check('number keys tune a channel, and it has moved on meanwhile', ok and rep['ch2-again']['tv']['channel'] == 2
      and rep['ch2-again']['tv']['nowMs'] - rep['ch2']['tv']['nowMs'] > 30000, d + f"; {(rep['ch2-again']['tv']['nowMs'] - rep['ch2']['tv']['nowMs']) / 1000:.0f} s after the first visit")
e = rep['empty']
# Snow is grey, so green in the corner can only be the channel display, drawn over it.
snow_green = green('tv-empty.png', (0.6, 0.0, 1.0, 0.3))
snow_any = green('tv-empty.png', (0.0, 0.3, 1.0, 1.0))   # the rest must be plain snow, not the last channel's picture
check('an empty channel shows static and says so, with the channel number readable over the snow', e['tv']['channel'] == 9 and e['tv']['message'] == 'NO PROGRAMMES'
      and e['staticMoment'] == 'static' and e['state'] != 'playing' and snow_green > 0.004 and snow_any < 0.001,
      f"\"{e['tv']['message']}\", {e['staticMoment']}; green lettering in {snow_green * 100:.1f}% of the top-right corner, {snow_any * 100:.2f}% green elsewhere")
e = rep['nosuch']
check('a number with no channel leaves the TV where it was', e['tv']['channel'] == 9 and 'NOT IN USE' in e['tv']['message'], f"\"{e['tv']['message']}\", still channel {e['tv']['channel']}")
e = rep['raced']
check('leaving for the empty channel while a programme is still opening: nothing starts playing behind the static', e['tv']['channel'] == 9 and e['state'] not in ('playing', 'loading')
      and e['staticMoment'] == 'static' and e['tv']['message'] == 'NO PROGRAMMES', f"{e['state']}, {e['staticMoment']}, \"{e['tv']['message']}\"")
e = rep['desk']
dk = Image.open(os.path.join(out, 'tv-desk.png')).convert('L')
lit = sum(1 for v in (dk.get_flattened_data() if hasattr(dk, 'get_flattened_data') else dk.getdata()) if v > 60) / (dk.size[0] * dk.size[1])
check('desk mode: channels change on the set, with the overlay drawn', e['tv']['on'] and e['tv']['channel'] == 3 and e['tv']['hasOverlay'] and e['state'] == 'playing' and lit > 0.03,
      f"channel {e['tv']['channel']}, overlay on, {lit * 100:.0f}% of the desk view lit by the set")
e = rep['off']
check('TV off: the programme stops; no overlay, no static', not e['tv']['on'] and not e['tv']['hasOverlay'] and e['staticMoment'] == 'none' and e['state'] == 'idle',
      f"{e['state']}; overlay: {e['tv']['hasOverlay']}, static: {e['staticMoment']}")

# ---- what TV mode must not touch
check('TV mode adds nothing to the playlist', all(e['playlistCount'] == 0 for e in rep.values() if e in [x for x in run1 if x['cmd'] == 'report']), 'playlist empty throughout')
touched = [p for p in glob.glob(os.path.join(data, '**', '*'), recursive=True) if os.path.isfile(p) and 'tv-media' in open(p, errors='ignore').read()]
check('TV mode saves no resume positions', not touched, ', '.join(touched) or 'no TV programme in the resume file')
reqs = [json.loads(l) for l in open(os.path.join(out, 'tv-jf', 'requests.jsonl'))]
sessions = [r for r in reqs if r['path'].startswith('/Sessions/Playing')]
streams = [r for r in reqs if r['path'].endswith('/stream')]
rec = [r for r in reqs if r['query'].get('Recursive') == 'true']
check('Jellyfin: the channel\'s episodes are listed in one recursive query', rec and rec[0]['query'].get('ParentId') == 'lib-shows'
      and 'Episode' in rec[0]['query'].get('IncludeItemTypes', ''), f"{len(rec)} request(s), ParentId {rec[0]['query'].get('ParentId') if rec else '-'}")
check('Jellyfin: TV mode reports no playback (resume points and "watched" marks stay as they are)', streams and not sessions,
      f'{len(streams)} stream requests, {len(sessions)} playback reports')
check('Jellyfin: streams still carry the token in the header only', all(r['authHeader'] and not r['tokenInUrl'] for r in streams), f'{len(streams)} requests')
store = os.path.join(cfg, 'CRTPlayer', 'CRTPlayer', 'channels.json')
txt = open(store).read() if os.path.exists(store) else ''
check('the channels are stored without any token', txt and 'tok-' not in txt and '"channels"' in txt, store.replace(cfg, '…') if txt else 'channels.json missing')

# ---- second launch
r = rep['restored']
wait = [e for e in run2 if e['cmd'].startswith('tvwait')][0]
ch2 = {c['number']: c for c in r['tv']['channels']}
check('the channels are back on the next launch, without reading the videos again', sorted(ch2) == [2, 3, 5, 9] and all(ch2[n]['programs'] == ch[n]['programs'] for n in ch2)
      and wait['waitedMs'] < 3000, f"channels {sorted(ch2)}, ready after {wait['waitedMs']:.0f} ms")
ok, d = in_step('on-again', 4500)
check('TV on again goes to the channel last watched', ok and rep['on-again']['tv']['channel'] == rep['off']['tv']['channel'], f"channel {rep['on-again']['tv']['channel']}; " + d)
e = rep['opened-file']
check('opening a file of your own turns the TV off', not e['tv']['on'] and e['playlistCount'] == 1 and e['file'].startswith('sd_4x3') and not e['tv']['hasOverlay'],
      f"TV on: {e['tv']['on']}; playing {e['file']}")
print(f"\n{fails} Cable TV check(s) failed" if fails else "\nAll Cable TV checks passed")
sys.exit(1 if fails else 0)
