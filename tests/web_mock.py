#!/usr/bin/env python3
"""A small video site for the tests of "videos from web sites" (2.17).

  web_mock.py --media DIR --port N [--log FILE] [--ytdlp FILE]

  /media/<file>                the clips of scripts/make-test-media.sh (DIR/web), with byte ranges.
                               "?expire=<unix time>": 403 once that time has passed (addresses that stop working)
                               "?kbps=N": sent no faster than that; "?stall=BYTE,SECONDS": the connection hangs
                               for that long, once, when it reaches that byte of the file (a network hiccup);
                               "?outage=AFTER,SECONDS": nothing of this file is sent for that long, beginning
                               AFTER seconds after it was first asked for (the network is away);
                               "?yt=KBPS": as YouTube's video servers seem to: every answer begins 80 ms late (the way
                               to the server and back), and one that asks for more than 10 MiB, or for everything from
                               a place on, comes no faster than KBPS (what yt-dlp's "http_chunk_size" is there for)
  /watch/muxed | dash | hls    pages with a video on them, for the real yt-dlp (its generic extractor):
                               an HTML5 video, a DASH manifest naming two files, an HLS stream
  /watch/none                  a page without a video
  /yt/...                      pages for tests/fake_ytdlp.py (it never fetches them; the player asks what they are)
  /stream                      a video file under an address that does not say so (only its content type does)
  /mislabeled                  a video file sent as "text/html"
  /untyped, /untyped-nothing   a video file, and something that is no video, sent as a type that says nothing
  /release/yt-dlp/...          what the player's "Get yt-dlp" fetches: SHA2-256SUMS and yt-dlp_linux (FILE)
  /release/deno/...            a zip archive with a stand-in "deno", and its checksum file
  /release/bad/...             the same, with checksums that do not match
  /feeds/videos.xml?channel_id=ID   a channel's feed of new videos (as YouTube's), for the browser (2.18);
                               the channel "Lighthouse Cinema" has none (404: the player asks yt-dlp instead)
  /thumbs/<file>               the pictures of the videos and channels (DIR/web/thumbs)
  /api/search?q=, /api/channel/ID, /api/video/ID   the site's videos (tests/web_catalog.py), for tests/fake_ytdlp.py
  /api/latest                  what GitHub says of yt-dlp's newest release ("?tag=" sets it)

Every request is written to the log (one JSON object a line)."""
import argparse, hashlib, http.server, io, json, os, re, socketserver, time, urllib.parse, zipfile
import web_catalog as cat

ap = argparse.ArgumentParser()
ap.add_argument('--media', required=True); ap.add_argument('--port', type=int, required=True)
ap.add_argument('--log'); ap.add_argument('--ytdlp')
args = ap.parse_args()
WEB = os.path.join(args.media, 'web')
log = open(args.log, 'a') if args.log else None

TYPES = {'mp4': 'video/mp4', 'm4a': 'audio/mp4', 'webm': 'video/webm', 'vtt': 'text/vtt', 'm3u8': 'application/vnd.apple.mpegurl',
         'mpd': 'application/dash+xml', 'ts': 'video/mp2t', 'html': 'text/html; charset=utf-8'}

PAGES = {
    'muxed': '<video controls src="/media/muxed.mp4"></video>',
    'dash': '<video controls><source src="/media/dash.mpd" type="application/dash+xml"></video>',
    'hls': '<video controls><source src="/media/hls/master.m3u8" type="application/x-mpegURL"></video>',
    'none': '<p>Nothing to watch here.</p>',
}

# What "Get yt-dlp" fetches.
DENO = b'#!/bin/sh\necho "deno 9.9.9 (stand-in)"\n'
def zipped(name, data):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
        info = zipfile.ZipInfo(name); info.external_attr = 0o755 << 16
        z.writestr(info, data + b'# ' + b'padding ' * 4000 + b'\n')   # (so that there is something to inflate)
    return buf.getvalue()
RELEASE = {}
if args.ytdlp:
    program = open(args.ytdlp, 'rb').read()
    sha = hashlib.sha256(program).hexdigest()
    lines = ''.join('%s  %s\n' % (sha, n) for n in ('yt-dlp', 'yt-dlp.exe', 'yt-dlp_linux', 'yt-dlp_linux_aarch64'))
    for n in ('yt-dlp_linux', 'yt-dlp_linux_aarch64', 'yt-dlp.exe', 'yt-dlp'):
        RELEASE['yt-dlp/' + n] = program
        RELEASE['bad/' + n] = program
    RELEASE['yt-dlp/SHA2-256SUMS'] = lines.encode()
    RELEASE['bad/SHA2-256SUMS'] = lines.replace(sha, hashlib.sha256(b'something else').hexdigest()).encode()
for asset, inner in (('deno-x86_64-unknown-linux-gnu.zip', 'deno'), ('deno-aarch64-unknown-linux-gnu.zip', 'deno'), ('deno-x86_64-pc-windows-msvc.zip', 'deno.exe')):
    z = zipped(inner, DENO)
    RELEASE['deno/' + asset] = z
    # (the Linux archives' checksum files are "<sum>  <name>"; the Windows one is what PowerShell prints)
    RELEASE['deno/' + asset + '.sha256sum'] = (('\nAlgorithm : SHA256\nHash      : %s\nPath      : C:\\a\\deno\\%s\n\n' % (hashlib.sha256(z).hexdigest().upper(), asset))
                                               if 'windows' in asset else '%s  %s\n' % (hashlib.sha256(z).hexdigest(), asset)).encode()
    RELEASE['bad/' + asset] = z
    RELEASE['bad/' + asset + '.sha256sum'] = ('%s  %s\n' % (hashlib.sha256(b'no').hexdigest(), asset)).encode()

STALLED = set()
FIRST = {}
LATEST = {'tag': '2026.09.30'}

class H(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.1'
    def log_message(self, *a): pass
    def note(self, status):
        if not log: return
        h = self.headers
        log.write(json.dumps({'t': round(time.time(), 3), 'method': self.command, 'path': self.path, 'range': h.get('Range'), 'status': status,
                              'ua': h.get('User-Agent'), 'referer': h.get('Referer'), 'cookie': '; '.join(h.get_all('Cookie') or []), 'x': h.get('X-Test')}) + '\n')
        log.flush()
    def plain(self, status, body=b'', ctype='text/plain'):
        self.note(status)
        self.send_response(status); self.send_header('Content-Type', ctype); self.send_header('Content-Length', str(len(body))); self.end_headers()
        if self.command != 'HEAD': self.wfile.write(body)
    def file(self, path, ctype=None, kbps=0, stall=None, outage=None, yt=0):
        if not os.path.isfile(path): return self.plain(404)
        first = FIRST.setdefault(self.path, time.time())   # (by the whole address: each test's own)
        def wait_out():   # the network is away from `outage[0]` seconds after the file was first asked for, for outage[1] seconds
            if outage:
                left = first + outage[0] + outage[1] - time.time()
                if 0 < left <= outage[1]: time.sleep(left)
        wait_out()
        size = os.path.getsize(path); start, end, status = 0, size - 1, 200
        m = re.match(r'bytes=(\d*)-(\d*)', self.headers.get('Range') or '')
        if m:
            if m.group(1): start = int(m.group(1))
            if m.group(2): end = min(int(m.group(2)), size - 1)
            status = 206
            if start >= size: return self.plain(416)
        if yt:
            time.sleep(0.08)
            if not (m and m.group(2)) or end - start + 1 > 10 << 20: kbps = yt
        self.note(status)
        self.send_response(status)
        self.send_header('Content-Type', ctype or TYPES.get(path.rsplit('.', 1)[-1], 'application/octet-stream'))
        self.send_header('Accept-Ranges', 'bytes'); self.send_header('Content-Length', str(end - start + 1))
        if status == 206: self.send_header('Content-Range', 'bytes %d-%d/%d' % (start, end, size))
        self.end_headers()
        if self.command == 'HEAD': return
        try:
            with open(path, 'rb') as f:
                f.seek(start); left = end - start + 1; sent = 0; began = time.time()
                while left > 0:
                    b = f.read(min(16384 if kbps or stall or outage else 65536, left))
                    if not b: break
                    wait_out()
                    self.wfile.write(b); left -= len(b); sent += len(b)
                    if kbps: time.sleep(max(0.0, sent * 8 / (kbps * 1000.0) - (time.time() - began)))   # no faster than that
                    if stall and start <= stall[0] < start + sent and path not in STALLED:                # the connection hangs once, there
                        STALLED.add(path); self.wfile.flush(); time.sleep(stall[1])
        except (BrokenPipeError, ConnectionResetError): pass
    def do_HEAD(self): self.do_GET()
    def do_GET(self):
        u = urllib.parse.urlsplit(self.path); q = urllib.parse.parse_qs(u.query); p = u.path
        if p.startswith('/media/'):
            if 'expire' in q and time.time() > float(q['expire'][0]): return self.plain(403, b'this address has expired')
            rel = os.path.normpath(p[len('/media/'):])
            if rel.startswith('..'): return self.plain(404)
            stall = [float(x) for x in q['stall'][0].split(',')] if 'stall' in q else None
            outage = [float(x) for x in q['outage'][0].split(',')] if 'outage' in q else None
            return self.file(os.path.join(WEB, rel), kbps=float(q.get('kbps', ['0'])[0]), stall=stall, outage=outage, yt=float(q.get('yt', ['0'])[0]))
        if p.startswith('/watch/') and p[7:] in PAGES:
            name = p[7:]
            return self.plain(200, ('<!doctype html><html><head><title>Mock video: %s</title></head><body>%s</body></html>' % (name, PAGES[name])).encode(),
                              TYPES['html'])
        if p.startswith('/yt/'): return self.plain(200, b'<!doctype html><html><head><title>A video page</title></head><body></body></html>', TYPES['html'])
        if p == '/stream': return self.file(os.path.join(WEB, 'muxed.mp4'), 'video/mp4')
        if p == '/mislabeled': return self.file(os.path.join(WEB, 'muxed.mp4'), 'text/html')
        if p == '/untyped': return self.file(os.path.join(WEB, 'muxed.mp4'), 'application/x-something')
        if p == '/untyped-nothing': return self.plain(200, b'not a video at all, and no page either\n' * 200, 'application/x-something')
        if p.startswith('/release/') and p[9:] in RELEASE: return self.plain(200, RELEASE[p[9:]], 'application/octet-stream')
        base = 'http://%s' % self.headers.get('Host')
        if p == '/feeds/videos.xml':
            c = cat.CATALOG.get(q.get('channel_id', [''])[0])
            if not c or c['handle'] in cat.NO_FEED: return self.plain(404, b'not found')
            return self.plain(200, cat.feed_xml(base, c), 'application/atom+xml; charset=UTF-8')
        if p.startswith('/thumbs/'):
            f = os.path.join(WEB, 'thumbs', os.path.basename(p))
            return self.file(f, 'image/jpeg') if os.path.isfile(f) else self.plain(404)
        if p == '/api/search': return self.plain(200, json.dumps(cat.search(q.get('q', [''])[0])).encode(), 'application/json')
        if p.startswith('/api/channel/'):
            c = cat.channel(urllib.parse.unquote(p[len('/api/channel/'):]))
            return self.plain(200, json.dumps(c).encode(), 'application/json') if c else self.plain(404)
        if p.startswith('/api/video/'):
            v = cat.VIDEOS.get(p[len('/api/video/'):])
            return self.plain(200, json.dumps(v).encode(), 'application/json') if v else self.plain(404)
        if p == '/api/latest':
            if 'tag' in q: LATEST['tag'] = q['tag'][0]
            return self.plain(200, json.dumps({'tag_name': LATEST['tag'], 'name': 'yt-dlp ' + LATEST['tag']}).encode(), 'application/json')
        return self.plain(404)

class S(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True; allow_reuse_address = True
S(('127.0.0.1', args.port), H).serve_forever()
