#!/usr/bin/env python3
"""Checks the run of the browser of web videos (tests/automation/browse.txt, 2.18).
Usage: check-browse.py OUT_DIR SITE TESTS_DIR      (SITE: the mock site, http://127.0.0.1:PORT; TESTS_DIR: tests/)

What the browser shows is compared with what the stand-in site holds (tests/web_catalog.py): which videos, in which
order, from which channels; and the screenshots with what the browser says it drew (its tiles' places)."""
import json, sys, pathlib
import numpy as np
from PIL import Image

out = pathlib.Path(sys.argv[1]); site = sys.argv[2]
sys.path.insert(0, sys.argv[3])
import web_catalog as cat

fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

try:
    log = json.load(open(out / 'browse.json'))
except Exception as e:
    print(f"FAIL  the run finished: {e}"); sys.exit(1)
rep = {e['label']: e for e in log if e['cmd'] == 'report'}
B = {k: v.get('browse', {}) for k, v in rep.items()}
failed = [e['cmd'] for e in log if e.get('ok') is False]
check('the run finished, every step as it should', 'end' in rep and not failed, f"{len(rep)} reports; failed steps: {failed}")

def cid(handle): return cat.chan_id(handle)
RETRO, NIGHT, PIXEL, LIGHT = cid('retrotubelab'), cid('nightdrivefm'), cid('pixelkitchen'), cid('lighthousecinema')
def titles_of(c): return [v['title'] for v in cat.CATALOG[c]['videos']]

# ---- following channels
e = B.get('b:empty-new', {})
check('nothing followed: New is empty, and says how to fill it', e.get('open') and e.get('section') == 'new' and e.get('count') == 0 and not e.get('error'),
      f"open {e.get('open')}, {e.get('section')}, {e.get('count')} tiles, error '{e.get('error')}'")
e = B.get('b:empty-channels', {})
check('Channels without any: adding one by its link, or Google Takeout', e.get('titles') == ['+add', '+import'], e.get('titles'))
e = B.get('b:link-asked', {})
check('"Add a channel" asks for its link', e.get('focus') == 'input' and e.get('inputChannel') is True, f"focus {e.get('focus')}, asking for a link: {e.get('inputChannel')}")
e = B.get('b:channel-added', {})
d = e.get('data', {})
check('a channel by its link ("…/@handle"): its page, its videos, and it is followed',
      e.get('section') == 'channel' and e.get('channel') == RETRO and e.get('channelTitle') == 'Retro Tube Lab' and e.get('titles') == titles_of(RETRO)[:12]
      and [c['id'] for c in d.get('channels', [])] == [RETRO] and d.get('channels', [{}])[0].get('avatar', '').endswith('/thumbs/a00.jpg'),
      f"{e.get('channelTitle')} ({e.get('channel')}), {e.get('count')} videos; followed: {[c.get('title') for c in d.get('channels', [])]}")
e = B.get('b:channels-one', {})
check('back to Channels: the one followed, then the two ways to add more', e.get('titles') == ['channel:Retro Tube Lab', '+add', '+import'], e.get('titles'))
e = B.get('b:imported', {})
ids = sorted(c['id'] for c in e.get('data', {}).get('channels', []))
check('Google Takeout\'s list: the three not followed yet are added (the one followed already and the line that is no channel are not)',
      ids == sorted([RETRO, NIGHT, PIXEL, LIGHT]) and e.get('message') == '3 channels added' and e.get('count') == 6,
      f"{len(ids)} channels; said: {e.get('message')}; tiles: {e.get('titles')}")

# ---- New: the newest videos of the channels followed
e = B.get('b:new', {})
want = sorted([v for c in (RETRO, NIGHT, PIXEL, LIGHT) for v in cat.CATALOG[c]['videos']], key=lambda v: -v['published'])
got_titles = e.get('titles', [])
check('New: the videos of the four channels, newest first (twelve to a page)', e.get('count') == len(want) and got_titles == [v['title'] for v in want[:12]] and e.get('pages') == 2,
      f"{e.get('count')} videos ({len(want)} on the site), {e.get('pages')} pages; first: {got_titles[:3]}")
check('the "shorts" in the feeds are left out (upright phone videos)', not any(t.startswith('A short:') for t in got_titles), 'none among them')
d = e.get('data', {})
check('a channel without a feed is asked of yt-dlp instead (and its videos are there)', d.get('feedFallbacks', 0) >= 1 and e.get('failedChannels') == 0
      and d.get('feedReads', 0) >= 4, f"feeds read {d.get('feedReads')}, through yt-dlp {d.get('feedFallbacks')}, failed {e.get('failedChannels')}")

def tiles_drawn(png, b, n):
    """How many of the first n tiles show a picture in the screenshot (not a plain fill), and whether the chosen one glows."""
    a = np.asarray(Image.open(png).convert('RGB'), dtype=float)
    ox, oy = [int(x) for x in b.get('origin', '0,0').split(',')]
    shown = 0
    for i, (x, y, w, h) in enumerate(b.get('cells', [])[:n]):
        x, y = x + ox, y + oy
        x0, y0 = int(x + w * 0.1), int(y + h * 0.1)
        part = a[y0:int(y + h * 0.55), x0:int(x + w * 0.9)]
        if part.size and part.std(axis=(0, 1)).mean() > 12: shown += 1
    glow = 0.0
    sel = b.get('selected', 0) % 12
    if b.get('cells'):
        x, y, w, h = b['cells'][sel]
        x, y = x + ox, y + oy
        # (the chosen tile is drawn 7.5% larger: its glow lies just outside that)
        top, bottom = y - h * 0.0375, y + h * 1.0375
        ring = np.concatenate([a[int(top - 8):int(top - 2), int(x + w * 0.2):int(x + w * 0.8)].reshape(-1, 3),
                               a[int(bottom + 2):int(bottom + 8), int(x + w * 0.2):int(x + w * 0.8)].reshape(-1, 3)])
        amber = (ring[:, 0] > 60) & (ring[:, 0] > ring[:, 1]) & (ring[:, 1] > ring[:, 2]) & (ring[:, 0] - ring[:, 2] > 25)   # (warm: the glow, soft)
        glow = amber.mean() if ring.size else 0
    return shown, glow
shown, glow = tiles_drawn(out / 'browse' / 'new.png', e, 12)
check('on the screen: each of the twelve tiles shows its picture, and the chosen one glows', shown == 12 and glow > 0.6,
      f"{shown} of 12 with a picture; {glow * 100:.0f}% of the edge around the chosen one in amber")

import os
e = B.get('b:paint', {})
timing = os.environ.get('BROWSE_NO_TIMING') != '1'   # (not under the sanitizers: everything is ten times slower there)
ok = e.get('paints', 0) >= 20 and e.get('paintMsAvg', 99) < 20
detail = f"{e.get('paints')} pictures painted, {e.get('paintMsAvg', 0):.1f} ms each on average, {e.get('paintMsMax', 0):.1f} ms at most (2.18.0: about 45 ms each)"
if timing:
    check('turning pages and moving from tile to tile: a picture of the menu takes little time to paint (1080p, this machine)', ok, detail)
else:
    print(f"INFO  painting (not judged here): {detail}")
e1, e2, e3 = B.get('b:moved', {}), B.get('b:page2', {}), B.get('b:back-left', {})
check('arrow keys: right, right, down', e1.get('selected') == 6, f"tile {e1.get('selected')}")
check('Page Down: the next page (its first tile)', e2.get('page') == 1 and e2.get('selected') == 12 and e2.get('titles') == [v['title'] for v in want[12:24]],
      f"page {e2.get('page')}, tile {e2.get('selected')}")
check('left from the first column: the page before, its last column', e3.get('page') == 0 and e3.get('selected') == 3, f"page {e3.get('page')}, tile {e3.get('selected')}")

# ---- a video's page
e = B.get('b:detail', {})
check('a video\'s page: what it is, and its buttons', e.get('detail') and e.get('detailTitle') == want[0]['title']
      and e.get('detailButtons') == ['Play', 'Watch later', 'Go to the channel', 'Unfollow the channel'], f"{e.get('detailTitle')}: {e.get('detailButtons')}")
e = B.get('b:detail-later', {})
check('"Watch later" there puts it aside (and the button says so)', want[0]['title'] in e.get('laterTitles', []) and e.get('detailButtons', [None, None])[1] == 'Remove from Watch later',
      f"put aside: {e.get('laterTitles')}")
e = B.get('b:later', {})
check('W on another video puts it aside too; Watch later lists them, the last first', e.get('titles') == [want[1]['title'], want[0]['title']], e.get('titles'))

# ---- search
e = B.get('b:search-asked', {})
check('/ goes to the search line', e.get('section') == 'search' and e.get('focus') == 'input', f"{e.get('section')}, {e.get('focus')}")
e = B.get('b:search', {})
found = [v['title'] for v in cat.search('tube tv')]
check('a search: what the site found, in its order', e.get('titles') == found[:12] and e.get('count') == len(found) and e.get('input') == 'tube tv',
      f"{e.get('count')} found ({len(found)} on the site): {e.get('titles', [])[:3]}")
shown, glow = tiles_drawn(out / 'browse' / 'search.png', e, min(12, len(found)))
check('on the screen: the search\'s tiles with their pictures', shown == min(12, len(found)), f"{shown} of {min(12, len(found))}")
e = B.get('b:search-waiting', {})
check('a slow answer: the places wait for it (empty, shimmering), not the last search\'s videos', e.get('loading') is True and e.get('count') == 0, f"loading {e.get('loading')}, {e.get('count')} tiles")
e = B.get('b:search-all', {})
check('thirty-six at most, three pages of twelve', e.get('count') == 36 and e.get('pages') == 3, f"{e.get('count')} videos, {e.get('pages')} pages")
e = B.get('b:search-page3', {})
check('to the third page', e.get('page') == 2 and e.get('selected') == 24, f"page {e.get('page')}, tile {e.get('selected')}")
e = B.get('b:search-fail', {})
check('YouTube refuses: yt-dlp\'s words are shown', '429' in e.get('error', '') and e.get('count') == 0, e.get('error'))
check('a newer yt-dlp is known (once a day, from its project\'s releases)', e.get('ytNewest') == '2026.10.09' and e.get('ytInstalled') == '2026.09.30',
      f"installed {e.get('ytInstalled')}, newest {e.get('ytNewest')}")
e = rep.get('b:updated', {})
check('U fetches it (the player\'s own copy), and the list is asked for again', e.get('online', {}).get('fetches', 0) >= 1 and e.get('browse', {}).get('updating') is False
      and 'yt-dlp' in e.get('browse', {}).get('message', ''), f"fetched {e.get('online', {}).get('fetches')}; said: {e.get('browse', {}).get('message')}")

# ---- the controller
e1, e2, e3 = rep.get('b:pad-rb', {}), rep.get('b:pad-lb', {}), rep.get('b:pad-x', {})
check('controller: the right shoulder button, the next section (after Search: New)', e1.get('browse', {}).get('section') == 'new', f"{e1.get('browse', {}).get('section')} ({e1.get('gamepadLastAction')})")
check('the left one, back', e2.get('browse', {}).get('section') == 'search', e2.get('browse', {}).get('section'))
check('X puts the chosen video aside', want[2]['title'] in e3.get('browse', {}).get('laterTitles', []) and e3.get('browse', {}).get('selected') == 2,
      f"tile {e3.get('browse', {}).get('selected')}; put aside: {e3.get('browse', {}).get('laterTitles')}")

# ---- playing
e = rep.get('b:playing', {})
h = (e.get('browse', {}).get('historyItems') or [{}])[0]
v0 = want[0]
check('Play: the menu goes and the video plays', e.get('browse', {}).get('open') is False and e.get('state') == 'playing', f"menu open {e.get('browse', {}).get('open')}, {e.get('state')}")
check('it goes into the history, with what the menu knew of it', h.get('title') == v0['title'] and h.get('channelId') == v0['channel_id'] and h.get('thumb', '').endswith('/thumbs/' + v0['thumb'])
      and h.get('channel') == v0['channel'], h)
e = rep.get('b:reopened', {})
check('opened again: the video waits meanwhile', e.get('browse', {}).get('open') is True and e.get('state') == 'paused', f"menu {e.get('browse', {}).get('open')}, video {e.get('state')}")
e = B.get('b:history-page', {})
check('its page in the history offers to resume where it was', (e.get('detailButtons') or [''])[0].startswith('Resume from 0:4') and 'Start over' in e.get('detailButtons', []),
      e.get('detailButtons'))
e1, e2 = rep.get('b:closed', {}), rep.get('b:plays-on', {})
check('closed: the video plays on', e1.get('browse', {}).get('open') is False and e2.get('state') == 'playing', f"menu {e1.get('browse', {}).get('open')}, video {e2.get('state')}")
e1, e2 = rep.get('b:guide', {}), rep.get('b:guide-b', {})
check('the controller\'s Guide button opens it, B closes it', e1.get('browse', {}).get('open') is True and e2.get('browse', {}).get('open') is False and e2.get('state') == 'playing',
      f"{e1.get('browse', {}).get('open')} → {e2.get('browse', {}).get('open')}, video {e2.get('state')}")

# ---- what was asked of yt-dlp and of the site
calls = [json.loads(l)['argv'] for l in open(out / 'browse-ytdlp.jsonl')] if (out / 'browse-ytdlp.jsonl').exists() else []
searches = [a for a in calls if a and a[-1].startswith('ytsearch')]
channels = [a for a in calls if a and '/channel/' in a[-1] or (a and '/@' in a[-1])]
check('a search asks yt-dlp for 36, as a flat list with dates', searches and all(a[-1].startswith('ytsearch36:') and '--flat-playlist' in a and 'youtubetab:approximate_date' in a for a in searches),
      f"{len(searches)} searches, e.g. {searches[0][-1] if searches else '-'}")
fallback = [a for a in channels if a[-1].endswith(LIGHT + '/videos')]
check('a channel\'s page asks for 48 videos; one without a feed for 15', any('--playlist-end' in a and a[a.index('--playlist-end') + 1] == '48' for a in channels)
      and fallback and all(a[a.index('--playlist-end') + 1] == '15' for a in fallback), f"{len(channels)} channel pages; without a feed: {len(fallback)}")
reqs = [json.loads(l) for l in open(out / 'browse-requests.jsonl')]
feeds = sorted({r['path'].split('channel_id=')[-1] for r in reqs if r['path'].startswith('/feeds/')})
check('the feeds of the channels followed are read (and only theirs)', feeds == sorted([RETRO, NIGHT, PIXEL, LIGHT]), feeds)
thumbs = [r for r in reqs if r['path'].startswith('/thumbs/')]
again = len(thumbs) - len({r['path'] for r in thumbs})
check('pictures are fetched once each (kept on disk, and in memory)', thumbs and again <= 2, f"{len(thumbs)} requests for {len({r['path'] for r in thumbs})} pictures")
others = sorted({r['path'].split('?')[0] for r in reqs if not r['path'].startswith(('/feeds/', '/thumbs/', '/api/', '/media/', '/yt/', '/release/'))})
check('nothing else was asked of the site', not others, others)

print(f"\n{fails} browse check(s) failed" if fails else "\nAll browse checks passed")
sys.exit(1 if fails else 0)
