#!/usr/bin/env python3
"""A stand-in for yt-dlp in the tests of "videos from web sites" (2.17): it answers the way the real one
answers for YouTube (the picture and the sound as two addresses, subtitles and automatic captions, chapters,
playlists, addresses that stop working), with the clips tests/web_mock.py hands out. Video sites cannot be
reached from where the tests run.

It is asked the way the player asks the real one (--dump-single-json ... -- URL) and looks at the page's address:

  .../yt/watch?v=pair-h264 | pair-vp9 | mixed-a | mixed-b     two addresses: H.264 + AAC, VP9 + Opus, H.264 + Opus, VP9 + AAC
  .../yt/watch?v=muxed | hls | big                            one file with both; an HLS stream; the large file + AAC
  .../yt/watch?v=expireN | expireNxM | dead                   as "big", its addresses working for N seconds (those of the first
                                                              M answers; later ones for an hour), or never
  .../yt/watch?v=outageN                                      as "big", over a slow line, and the network is away for a while
  .../yt/watch?v=gone | nojs | slow | headers | live          an error; the "no JavaScript runtime" warning; three seconds
                                                              to answer; headers and cookies to send along; a live stream
  .../yt/s/ID                                                 a short address of .../yt/watch?v=ID
  .../yt/playlist?list=NAME                                   a playlist of three

FAKE_YTDLP_LOG: a file every call is written to. A copy in a folder named "old" does not know --js-runtimes
(as yt-dlp before late 2025)."""
import json, os, sys, time, urllib.parse

argv = sys.argv[1:]
if os.environ.get('FAKE_YTDLP_LOG'):
    with open(os.environ['FAKE_YTDLP_LOG'], 'a') as f: f.write(json.dumps({'t': round(time.time(), 3), 'argv': argv, 'self': os.path.abspath(__file__)}) + '\n')
old = os.path.basename(os.path.dirname(os.path.abspath(__file__))) == 'old'
if '--version' in argv:
    print('2024.08.06' if old else '2026.09.30'); sys.exit(0)
if old and '--js-runtimes' in argv:
    sys.stderr.write('Usage: yt-dlp [OPTIONS] URL [URL...]\n\nyt-dlp: error: no such option: --js-runtimes\n'); sys.exit(2)
if '--' not in argv or '--dump-single-json' not in argv:
    sys.stderr.write('yt-dlp: error: the stand-in only answers --dump-single-json ... -- URL\n'); sys.exit(2)
url = argv[argv.index('--') + 1]
u = urllib.parse.urlsplit(url); q = urllib.parse.parse_qs(u.query)
base = '%s://%s' % (u.scheme, u.netloc)
media = base + '/media/'

def fail(text, code=1):
    sys.stderr.write('ERROR: %s\n' % text); sys.exit(code)

if u.path == '/yt/playlist':
    name = q.get('list', ['?'])[0]
    print(json.dumps({'_type': 'playlist', 'id': name, 'title': 'Three videos (%s)' % name, 'extractor_key': 'YoutubeTab', 'webpage_url': url,
                      'entries': [{'_type': 'url', 'ie_key': 'Youtube', 'id': i, 'url': '%s/yt/watch?v=%s' % (base, i), 'title': t, 'duration': 120}
                                  for i, t in (('pair-h264', 'First of three'), ('pair-vp9', 'Second of three'), ('muxed', 'Third of three'))]}))
    sys.exit(0)
if u.path == '/mislabeled':   # (the real one looks at the first bytes: not a page, so "a direct video link")
    print(json.dumps({'id': 'mislabeled', 'title': 'mislabeled', 'extractor_key': 'Generic', 'webpage_url': url, 'url': url, 'protocol': 'http',
                      'direct': True, 'ext': 'mp4', 'http_headers': {}}))
    sys.exit(0)
if u.path.startswith('/yt/s/'): vid = u.path[len('/yt/s/'):]
elif u.path == '/yt/watch': vid = q.get('v', [''])[0]
else: fail('Unsupported URL: %s' % url)

HEAD = {'User-Agent': 'Mozilla/5.0 (X11; Linux x86_64) stand-in/1.0', 'Accept': '*/*', 'Accept-Language': 'en-us,en;q=0.5', 'Sec-Fetch-Mode': 'navigate'}
def fmt(file, kind, **more):
    f = {'url': media + file, 'protocol': 'http', 'ext': file.rsplit('.', 1)[-1], 'format_id': file, 'http_headers': dict(HEAD)}
    if kind == 'v': f.update(vcodec=more.pop('vcodec'), acodec='none', width=640, height=360, fps=25, dynamic_range='SDR')
    else: f.update(vcodec='none', acodec=more.pop('acodec'), abr=96)
    f.update(more)
    return f
H264 = lambda: fmt('v_h264.mp4', 'v', vcodec='avc1.64001e')
VP9 = lambda: fmt('v_vp9.webm', 'v', vcodec='vp09.00.21.08')
AAC = lambda: fmt('a_aac.m4a', 'a', acodec='mp4a.40.2')
OPUS = lambda: fmt('a_opus.webm', 'a', acodec='opus')
BIG = lambda: fmt('v_big.mp4', 'v', vcodec='avc1.64001f', width=1280, height=720)

info = {'id': vid, 'title': 'Stand-in video (%s)' % vid, 'uploader': 'The test channel', 'extractor_key': 'Youtube', 'extractor': 'youtube',
        'webpage_url': '%s/yt/watch?v=%s' % (base, vid), 'original_url': url, 'duration': 120, 'is_live': False, 'language': 'en',
        'subtitles': {'en': [{'ext': 'json3', 'url': media + 'en.json3', 'name': 'English'}, {'ext': 'vtt', 'url': media + 'en.vtt', 'name': 'English'}],
                      'fr': [{'ext': 'vtt', 'url': media + 'fr.vtt', 'name': 'French'}],
                      'live_chat': [{'ext': 'json', 'url': media + 'chat.json', 'protocol': 'youtube_live_chat_replay'}]},
        'automatic_captions': {'en-orig': [{'ext': 'vtt', 'url': media + 'auto.vtt', 'name': 'English (Original)'}],
                               'en': [{'ext': 'vtt', 'url': media + 'auto.vtt', 'name': 'English'}],
                               'de': [{'ext': 'vtt', 'url': media + 'auto.vtt?tlang=de', 'name': 'German'}],
                               'ja': [{'ext': 'vtt', 'url': media + 'auto.vtt?tlang=ja', 'name': 'Japanese'}]},
        'chapters': [{'start_time': 0.0, 'end_time': 30.0, 'title': 'The beginning'}, {'start_time': 30.0, 'end_time': 70.0, 'title': 'The middle'},
                     {'start_time': 70.0, 'end_time': 120.0, 'title': 'The end'}]}

pairs = {'pair-h264': (H264, AAC), 'pair-vp9': (VP9, OPUS), 'mixed-a': (H264, OPUS), 'mixed-b': (VP9, AAC), 'big': (BIG, AAC)}
if vid in pairs:
    info['requested_formats'] = [f() for f in pairs[vid]]
elif vid.startswith('expire') or vid == 'dead':
    # "expire6": the addresses work for six seconds, "expire6x2": so do those of the second answer; the answers
    # after that are good for an hour. "dead": they never work.
    life, _, times = vid[len('expire'):].partition('x')
    asked = sum(1 for l in open(os.environ['FAKE_YTDLP_LOG']) if json.loads(l)['argv'][-1:] == [url]) if os.environ.get('FAKE_YTDLP_LOG') else 1
    until = 1.0 if vid == 'dead' else time.time() + (float(life or 5) if asked <= int(times or 1) else 3600)
    info['requested_formats'] = [BIG(), AAC()]
    for f in info['requested_formats']: f['url'] += '?expire=%.3f' % until
    info['subtitles'] = {}; info['automatic_captions'] = {}; info['chapters'] = []
elif vid.startswith('outage'):
    # The picture's stream comes over a line a little faster than the video, and the network is away for six
    # seconds, eight seconds in. (The number after "outage" only makes the address a new one.)
    info['requested_formats'] = [BIG(), AAC()]
    info['requested_formats'][0]['url'] += '?kbps=7000&outage=8,6&n=' + vid[len('outage'):]
    info['requested_formats'][0]['tbr'] = 6000
    info['subtitles'] = {}; info['automatic_captions'] = {}; info['chapters'] = []
elif vid == 'muxed':
    info.update(url=media + 'muxed.mp4', protocol='http', ext='mp4', vcodec='avc1.64001e', acodec='mp4a.40.2', width=640, height=360, fps=25,
                http_headers=dict(HEAD), format_id='18')
elif vid in ('hls', 'live'):
    info.update(url=media + 'hls/index.m3u8', manifest_url=media + 'hls/master.m3u8', protocol='m3u8_native', ext='mp4', vcodec='avc1.64001e',
                acodec='mp4a.40.2', width=640, height=360, fps=25, http_headers=dict(HEAD), format_id='93', is_live=vid == 'live')
    info['subtitles'] = {}; info['automatic_captions'] = {}; info['chapters'] = []
elif vid == 'headers':
    info['requested_formats'] = [H264(), AAC()]
    for f in info['requested_formats']:
        f['http_headers'].update({'Referer': base + '/yt/', 'X-Test': 'sent-along'})
        f['cookies'] = 'session=abc123; Domain=127.0.0.1; Path=/; Secure; pref=wide; Domain=127.0.0.1; Path=/'
elif vid == 'nojs':
    sys.stderr.write('WARNING: [youtube] No supported JavaScript runtime could be found. Only deno is enabled by default; to use another runtime add  '
                     '--js-runtimes RUNTIME[:PATH]  to your command/config. YouTube extraction without a JS runtime has been deprecated, and some '
                     'formats may be missing. See  https://github.com/yt-dlp/yt-dlp/wiki/EJS  for details on installing one\n')
    info['requested_formats'] = [H264(), AAC()]
elif vid == 'slow':
    time.sleep(3)
    info['requested_formats'] = [H264(), AAC()]
elif vid == 'gone':
    sys.stderr.write('WARNING: [youtube] gone: a warning that says nothing\n')
    fail('[youtube] gone: Video unavailable. This video is private')
else:
    fail('[youtube] %s: Video unavailable' % vid)
print(json.dumps(info))
