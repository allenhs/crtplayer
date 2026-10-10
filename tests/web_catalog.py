"""The browser's stand-in site (2.18): channels and their videos, made up, the same on every run (but for
the times, which are counted back from when the site was started). Used by tests/web_mock.py."""
import re, time

NOW = time.time()
CHANNELS = [
    ('Retro Tube Lab', 'retrotubelab', ['Restoring a 1984 console TV: the flyback transformer', 'Why scanlines make old games look better',
        'Convergence, purity and the degaussing coil', 'Composite, S-Video or RGB? A side-by-side', 'Fixing a TV that only shows a line',
        'The last tube TV factory, a visit', 'Shadow masks and aperture grilles explained', 'Bench test: five portable TVs from the 80s']),
    ('Night Drive FM', 'nightdrivefm', ['Neon Highway, a two-hour synthwave mix', 'Midnight city drive (no talking)', 'Outrun the dawn: retro mix vol. 7',
        'Rain on the windshield, lo-fi beats', 'Sunset boulevard, 1987 (an imagined broadcast)', 'Arcade nights: chiptune and synth']),
    ('Pixel Kitchen', 'pixelkitchen', ['Cooking every dish from a 16-bit adventure', 'The perfect pixel pancake', 'A diner breakfast, made in one pan',
        'Street food from the arcade era', 'Three soups for a cold evening', 'Bread from scratch, the slow way']),
    ('The Analog Archive', 'analogarchive', ['A Saturday morning in 1989: the full broadcast', 'Station idents from around the world',
        'Late-night test cards and their music', 'Weather reports of the 70s', 'The first music videos on cable']),
    ('Signal & Noise', 'signalnoise', ['Building a tiny TV transmitter (legally)', 'What a VCR actually does to your picture', 'Oscilloscope music, explained',
        'Repairing a broken tape deck', 'The sound of dial-up, slowed down']),
    ('Coastline Walks', 'coastlinewalks', ['Walking the old pier at sunrise (4K)', 'Fog over the harbour, ambient sound', 'A lighthouse at night, two hours',
        'Tide pools and seabirds', 'The longest beach walk, uncut']),
    ('Lighthouse Cinema', 'lighthousecinema', ['Why old film looks warm', 'A tour of a 1950s projection booth', 'Making a trailer the old way']),
    ('Arcade Afterhours', 'arcadeafterhours', ['Every cabinet in a closed arcade, one last time', 'High score attempt: the space shooter', 'Restoring a joystick panel']),
]
NO_FEED = {'lighthousecinema'}   # its feed is not there: the player asks yt-dlp for the channel instead


def chan_id(handle):
    return 'UC' + (handle + 'x' * 22)[:22]


def stable(text):   # (Python's hash() differs from run to run)
    h = 0
    for c in text.encode():
        h = (h * 131 + c) & 0xffffffff
    return h


CATALOG, VIDEOS = {}, {}
for ci, (name, handle, titles) in enumerate(CHANNELS):
    cid = chan_id(handle)
    vids = []
    for k, t in enumerate(titles):
        vid = 'cat-%s-%02d' % (handle[:8], k)
        h = stable(t)
        v = {'id': vid, 'title': t, 'channel': name, 'channel_id': cid, 'duration': 180 + (h % 2400), 'views': 1000 + (h * 37) % 900000,
             'published': NOW - (ci * 5 + k * 27 + 2) * 3600, 'thumb': 't%02d.jpg' % ((ci * 7 + k * 3) % 48),
             'description': 'From %s: %s. A video of the stand-in site the tests of CRT Player use.' % (name, t.lower())}
        vids.append(v)
        VIDEOS[vid] = v
    CATALOG[cid] = {'id': cid, 'title': name, 'handle': handle, 'avatar': 'a%02d.jpg' % (ci % 12), 'videos': vids}


def esc(t):
    return t.replace('&', '&amp;').replace('<', '&lt;')


def feed_xml(base, c):
    """A channel's feed, written as YouTube writes them (one "short" among the videos, as there often is)."""
    items = list(c['videos'])
    short = dict(items[0], id=items[0]['id'] + '-short', title='A short: ' + items[0]['title'], published=NOW - 1800)
    entries = []
    for v in [short] + items:
        link = '%s/%s%s' % (base, 'shorts/' if v['id'].endswith('-short') else 'yt/watch?v=', v['id'])
        pub = time.strftime('%Y-%m-%dT%H:%M:%S+00:00', time.gmtime(v['published']))
        entries.append(
            '<entry><id>yt:video:{id}</id><yt:videoId>{id}</yt:videoId><yt:channelId>{cid}</yt:channelId><title>{title}</title>'
            '<link rel="alternate" href="{link}"/><author><name>{channel}</name><uri>{base}/channel/{cid}</uri></author>'
            '<published>{pub}</published><updated>{pub}</updated><media:group><media:title>{title}</media:title>'
            '<media:content url="{base}/v/{id}" type="application/x-shockwave-flash" width="640" height="390"/>'
            '<media:thumbnail url="{base}/thumbs/{thumb}" width="480" height="360"/><media:description>{desc}</media:description>'
            '<media:community><media:starRating count="12" average="5.00" min="1" max="5"/><media:statistics views="{views}"/>'
            '</media:community></media:group></entry>'.format(id=v['id'], cid=c['id'], title=esc(v['title']), link=link, channel=esc(c['title']),
                                                              base=base, pub=pub, thumb=v['thumb'], desc=esc(v['description']), views=v['views']))
    return ('<?xml version="1.0" encoding="UTF-8"?>\n<feed xmlns:yt="http://www.youtube.com/xml/schemas/2015" '
            'xmlns:media="http://search.yahoo.com/mrss/" xmlns="http://www.w3.org/2005/Atom">'
            '<link rel="self" href="{base}/feeds/videos.xml?channel_id={cid}"/><id>yt:channel:{cid}</id><yt:channelId>{cid}</yt:channelId>'
            '<title>{title}</title><author><name>{title}</name><uri>{base}/channel/{cid}</uri></author>{entries}</feed>'
            .format(base=base, cid=c['id'], title=esc(c['title']), entries=''.join(entries))).encode()


def search(q):
    q = q.strip().lower()
    if q == 'everything':
        return sorted(VIDEOS.values(), key=lambda v: -v['published'])
    words = [w for w in re.split(r'\W+', q) if w]
    scored = []
    for v in VIDEOS.values():
        text = (v['title'] + ' ' + v['channel']).lower()
        n = sum(1 for w in words if w in text)
        if n:
            scored.append((-n, -v['published'], v['id']))
    return [VIDEOS[i] for _, _, i in sorted(scored)]


def channel(key):
    """By its id, or by its handle ("@retrotubelab")."""
    return CATALOG.get(key) or next((c for c in CATALOG.values() if c['handle'] == key.lstrip('@')), None)
