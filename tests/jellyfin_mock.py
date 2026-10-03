#!/usr/bin/env python3
"""A small mock of the Jellyfin HTTP API, for testing CRT Player's Jellyfin client.

It implements only the endpoints the player uses, following the public Jellyfin API:
public info, authentication, user views/items/resume (old routes for servers before
10.9, the newer /UserViews, /Items?userId=, /UserItems/Resume routes from 10.9 on),
primary images, static video streams with HTTP range support, paged listings
(StartIndex/Limit/TotalRecordCount), PlaybackInfo with a device-profile decision, HLS
conversion (made with ffmpeg, as the real server does; segments authenticated by the
api_key in their address), ending conversions, playback reporting and logout. Every
request is appended to a JSON-lines log for the test assertions.

This is NOT Jellyfin. Passing against it shows the client speaks the documented API;
it does not prove compatibility with every real server release.

Usage: jellyfin_mock.py --media DIR --port 18096 --version 10.10.3 --log requests.jsonl
"""
import argparse, io, json, mimetypes, os, re, secrets, shutil, subprocess, tempfile, threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs

TICKS = 10_000_000
USER = {"Id": "u1", "Name": "demo"}
PASSWORD = "crt"

def items_db():
    return {
        "lib-movies": {"Id": "lib-movies", "Name": "Movies", "Type": "CollectionFolder", "CollectionType": "movies", "IsFolder": True},
        "lib-shows": {"Id": "lib-shows", "Name": "Shows", "Type": "CollectionFolder", "CollectionType": "tvshows", "IsFolder": True},
        "m1": {"Id": "m1", "Name": "Test Pattern 4x3", "Type": "Movie", "MediaType": "Video", "ProductionYear": 2024,
               "RunTimeTicks": 20 * TICKS, "Parent": "lib-movies", "File": "sd_4x3_h264.mp4", "Overview": "A 4:3 test pattern.",
               "Codecs": ("mp4", "h264", "aac", 1_000_000),
               "MediaSources": [{"Id": "m1", "MediaStreams": [
                   {"Type": "Video", "Index": 0}, {"Type": "Audio", "Index": 1},
                   {"Type": "Subtitle", "Index": 2, "IsExternal": True, "Codec": "subrip", "Language": "eng",
                    "DisplayTitle": "English (SRT, external)"}]}]},
        "m2": {"Id": "m2", "Name": "Multitrack HD", "Type": "Movie", "MediaType": "Video", "ProductionYear": 2023,
               "RunTimeTicks": 30 * TICKS, "Parent": "lib-movies", "File": "hd_16x9_multitrack.mkv", "Resume": 12 * TICKS,
               # Declared as a high-bitrate file: over a 4 Mbit/s quality limit, the server converts it.
               "Codecs": ("mkv", "h264", "aac", 25_000_000),
               "MediaSources": [{"Id": "m2", "MediaStreams": [
                   {"Type": "Video", "Index": 0}, {"Type": "Audio", "Index": 1}, {"Type": "Audio", "Index": 2},
                   {"Type": "Subtitle", "Index": 3, "Codec": "subrip", "Language": "eng", "DisplayTitle": "English (embedded)"}]}]},
        "m4": {"Id": "m4", "Name": "Studio Master", "Type": "Movie", "MediaType": "Video", "ProductionYear": 2022,
               "RunTimeTicks": 20 * TICKS, "Parent": "lib-movies", "File": "sd_4x3_h264.mp4",
               # A codec no GStreamer install decodes: the server must convert it.
               "Codecs": ("mov", "prores", "pcm_s24le", 150_000_000)},
        "m5": {"Id": "m5", "Name": "Damaged Upload", "Type": "Movie", "MediaType": "Video", "ProductionYear": 2021,
               "RunTimeTicks": 20 * TICKS, "Parent": "lib-movies", "File": "@damaged", "TranscodeFrom": "sd_4x3_h264.mp4",
               # Looks playable, but the original won't play here: the player asks for a conversion.
               "Codecs": ("mp4", "h264", "aac", 1_000_000)},
        "m3": {"Id": "m3", "Name": "Vertical Clip", "Type": "Movie", "MediaType": "Video", "ProductionYear": 2025,
               "RunTimeTicks": 15 * TICKS, "Parent": "lib-movies", "File": "vertical_9x16_h264.mp4"},
        "s1": {"Id": "s1", "Name": "Test Show", "Type": "Series", "IsFolder": True, "Parent": "lib-shows"},
        "se1": {"Id": "se1", "Name": "Season 1", "Type": "Season", "IsFolder": True, "Parent": "s1", "IndexNumber": 1},
        "e1": {"Id": "e1", "Name": "Pilot", "Type": "Episode", "MediaType": "Video", "Parent": "se1", "SeriesName": "Test Show",
               "IndexNumber": 1, "ParentIndexNumber": 1, "RunTimeTicks": 10 * TICKS, "File": "ultrawide_64x27_vp9.webm",
               "Codecs": ("webm", "vp9", "opus", 2_000_000)},
        "e2": {"Id": "e2", "Name": "Second Episode", "Type": "Episode", "MediaType": "Video", "Parent": "se1", "SeriesName": "Test Show",
               "IndexNumber": 2, "ParentIndexNumber": 1, "RunTimeTicks": 10 * TICKS, "File": "anamorphic_dvd_mpeg2.mkv",
               "Codecs": ("mkv", "mpeg2video", "ac3", 400_000)},
        "lib-big": {"Id": "lib-big", "Name": "Big Library", "Type": "CollectionFolder", "CollectionType": "movies", "IsFolder": True},
    }

BIG = 1234   # items in "Big Library": more than a page, more than the old 500 limit

def big_items():
    return {f"b{n:04d}": {"Id": f"b{n:04d}", "Name": f"Clip {n:04d}", "Type": "Movie", "MediaType": "Video",
                          "ProductionYear": 2000 + n % 25, "RunTimeTicks": 20 * TICKS, "Parent": "lib-big",
                          "File": "sd_4x3_h264.mp4", "Codecs": ("mp4", "h264", "aac", 1_000_000)} for n in range(1, BIG + 1)}

class State:
    def __init__(self, args):
        self.args = args
        self.items = items_db()
        self.items.update(big_items())
        self.hls = {}                      # item id -> directory with its converted HLS
        self.hls_lock = threading.Lock()
        self.tmp = tempfile.mkdtemp(prefix="jfmock-")
        self.damaged = os.path.join(self.tmp, "damaged.mp4")
        with open(self.damaged, "wb") as f:   # an MP4 header followed by garbage
            f.write(b"\x00\x00\x00\x18ftypmp42\x00\x00\x00\x00mp42isom" + os.urandom(200_000))
        self.tokens = set()
        self.lock = threading.Lock()
        v = [int(x) for x in args.version.split('.')[:2]]
        self.modern = v >= [10, 9]

    def log(self, entry):
        with self.lock, open(self.args.log, 'a') as f:
            f.write(json.dumps(entry) + '\n')

    def dto(self, it):
        d = {k: v for k, v in it.items() if k not in ('Parent', 'File', 'Resume', 'Codecs', 'TranscodeFrom')}
        d.setdefault("IsFolder", False)
        d["ImageTags"] = {"Primary": "tag-" + it["Id"]}
        d["UserData"] = {"PlaybackPositionTicks": it.get("Resume", 0), "Played": it.get("Played", False)}
        return d

class Handler(BaseHTTPRequestHandler):
    server_version = "MockJellyfin/1.0"

    def log_message(self, *a):
        pass

    # --- helpers -----------------------------------------------------------------
    def token(self):
        for h in ("Authorization", "X-Emby-Authorization"):
            m = re.search(r'Token="([^"]+)"', self.headers.get(h, ""))
            if m:
                return m.group(1)
        return None

    def send_json(self, obj, status=200):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
        return status

    def fail(self, status):
        self.send_response(status)
        self.send_header("Content-Length", "0")
        self.end_headers()
        return status

    def record(self, status, extra=None):
        u = urlparse(self.path)
        q = parse_qs(u.query)
        e = {"method": self.command, "path": u.path, "query": {k: v[0] for k, v in q.items()},
             "authHeader": self.token() is not None,
             "tokenInUrl": any(k.lower() in ("api_key", "apikey", "token") for k in q),
             "range": self.headers.get("Range"), "status": status,
             "userAgent": self.headers.get("User-Agent", "")}
        if extra:
            e.update(extra)
        self.server.state.log(e)

    # --- routing -------------------------------------------------------------------
    def do_GET(self):
        self.dispatch("GET", None)

    def do_DELETE(self):
        self.dispatch("DELETE", None)

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0") or 0)
        raw = self.rfile.read(n) if n else b""
        try:
            body = json.loads(raw) if raw else {}
        except ValueError:
            body = {}
        self.dispatch("POST", body)

    def dispatch(self, method, body):
        st = self.server.state
        u = urlparse(self.path)
        path, q = u.path, {k: v[0] for k, v in parse_qs(u.query).items()}
        extra = {"body": body} if body else None
        try:
            if method == "GET" and path == "/System/Info/Public":
                status = self.send_json({"ServerName": "Mock Jellyfin", "Version": st.args.version, "Id": "srv1",
                                         "ProductName": "Jellyfin Server"})
                return self.record(status, extra)
            if method == "POST" and path == "/Users/AuthenticateByName":
                if (body or {}).get("Username") == USER["Name"] and (body or {}).get("Pw") == PASSWORD:
                    tok = "tok-" + secrets.token_hex(8)
                    st.tokens.add(tok)
                    status = self.send_json({"AccessToken": tok, "User": USER, "ServerId": "srv1"})
                else:
                    status = self.fail(401)
                return self.record(status, {"body": {"Username": (body or {}).get("Username")}})   # never log passwords
            tok = self.token()
            if path.lower().startswith("/videos/") and ("/hls1/" in path or path.endswith(".m3u8")):
                # HLS: like the real server, authenticated by the api_key in the address
                # (the segments are fetched without the client's headers).
                tok = q.get("api_key") or q.get("ApiKey")
            if tok not in st.tokens:
                return self.record(self.fail(401), extra)
            status = self.route(method, path, q, body or {}, tok)
            return self.record(status, extra)
        except (BrokenPipeError, ConnectionResetError):
            self.record(499, extra)

    def listing(self, items, q=None):
        q = q or {}
        start = int(q.get("StartIndex", 0))
        limit = int(q["Limit"]) if "Limit" in q else len(items)
        page = items[start:start + limit]
        return self.send_json({"Items": [self.server.state.dto(i) for i in page], "TotalRecordCount": len(items),
                               "StartIndex": start})

    def route(self, method, path, q, body, tok):
        st = self.server.state
        uid = USER["Id"]
        # Version-dependent routes: a pre-10.9 mock only knows the old ones, a newer one only the new ones.
        old = {"views": f"/Users/{uid}/Views", "items": f"/Users/{uid}/Items", "resume": f"/Users/{uid}/Items/Resume"}
        new = {"views": "/UserViews", "items": "/Items", "resume": "/UserItems/Resume"}
        r = new if st.modern else old
        if st.modern and path in (r["views"], r["items"], r["resume"]) and q.get("userId") != uid:
            return self.fail(400)
        if method == "GET" and path == f"/Users/{uid}":
            return self.send_json(USER)
        if method == "GET" and path == r["views"]:
            return self.listing([st.items["lib-movies"], st.items["lib-shows"], st.items["lib-big"]])
        if method == "GET" and path == r["resume"]:
            return self.listing([i for i in st.items.values() if i.get("Resume", 0) > 0 and not i.get("Played")])
        if method == "GET" and path == r["items"]:
            if "SearchTerm" in q:
                t = q["SearchTerm"].lower()
                return self.listing([i for i in st.items.values() if t in i["Name"].lower() and i["Type"] != "CollectionFolder"], q)
            if q.get("Recursive") == "true":
                # every descendant of ParentId, of the asked types
                types = set((q.get("IncludeItemTypes") or "").split(","))
                def under(i):
                    p = i.get("Parent")
                    while p:
                        if p == q.get("ParentId"):
                            return True
                        p = st.items.get(p, {}).get("Parent")
                    return False
                return self.listing([i for i in st.items.values() if under(i) and (not types or i["Type"] in types)], q)
            return self.listing([i for i in st.items.values() if i.get("Parent") == q.get("ParentId")], q)
        m = re.fullmatch(r"/Users/%s/Items/(\w+)" % uid, path) if not st.modern else re.fullmatch(r"/Items/(\w[\w-]*)", path)
        if method == "GET" and m and m.group(1) in st.items:
            return self.send_json(st.dto(st.items[m.group(1)]))
        m = re.fullmatch(r"/Items/([\w-]+)/Images/Primary", path)
        if method == "GET" and m and m.group(1) in st.items:
            return self.image(st.items[m.group(1)])
        m = re.fullmatch(r"/Videos/(\w+)/(\w+)/Subtitles/(\d+)/0/Stream\.(\w+)", path)
        if method == "GET" and m and m.group(1) in st.items:
            body = b"1\n00:00:00,000 --> 00:10:00,000\nJELLYFIN EXTERNAL SUBTITLE\n"
            self.send_response(200)
            self.send_header("Content-Type", "text/plain; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return 200
        m = re.fullmatch(r"/Videos/(\w+)/stream", path)
        if method == "GET" and m and m.group(1) in st.items and "File" in st.items[m.group(1)]:
            fn = st.items[m.group(1)]["File"]
            return self.stream(st.damaged if fn == "@damaged" else os.path.join(st.args.media, fn))
        m = re.fullmatch(r"/Items/(\w[\w-]*)/PlaybackInfo", path)
        if method == "POST" and m and m.group(1) in st.items:
            return self.playback_info(st.items[m.group(1)], body, q, tok)
        m = re.fullmatch(r"/videos/(\w+)/(master\.m3u8|main\.m3u8|hls1/main/(\d+)\.ts)", path, re.I)
        if method == "GET" and m and m.group(1) in st.items:
            return self.hls(st.items[m.group(1)], m.group(2), m.group(3), q)
        if method == "DELETE" and path == "/Videos/ActiveEncodings":
            return self.fail(204)
        if method == "POST" and path in ("/Sessions/Playing", "/Sessions/Playing/Progress", "/Sessions/Playing/Stopped"):
            it = st.items.get(body.get("ItemId"))
            if it is not None and path != "/Sessions/Playing":
                pos = int(body.get("PositionTicks", 0))
                if path.endswith("Stopped") and pos >= it.get("RunTimeTicks", 1) * 0.9:
                    it["Played"], it["Resume"] = True, 0
                else:
                    it["Resume"] = pos
            return self.fail(204)
        if method == "POST" and path == "/Sessions/Logout":
            st.tokens.discard(tok)
            return self.fail(204)
        return self.fail(404)

    # --- PlaybackInfo and HLS conversion -------------------------------------------------
    def playback_info(self, it, body, q, tok):
        prof = body.get("DeviceProfile") or {}
        dp = (prof.get("DirectPlayProfiles") or [{}])[0]
        lists = {k: [c.strip().lower() for c in (dp.get(k) or "").split(",") if c.strip()] for k in ("Container", "VideoCodec", "AudioCodec")}
        cont, vc, ac, rate = it.get("Codecs", ("mp4", "h264", "aac", 1_000_000))
        cap = int(body.get("MaxStreamingBitrate") or prof.get("MaxStreamingBitrate") or 10**9)
        reasons = []
        if cont not in lists["Container"]: reasons.append("ContainerNotSupported")
        if vc not in lists["VideoCodec"]: reasons.append("VideoCodecNotSupported")
        if ac not in lists["AudioCodec"]: reasons.append("AudioCodecNotSupported")
        if rate > cap: reasons.append("ContainerBitrateExceedsLimit")
        direct = bool(body.get("EnableDirectPlay", True)) and not reasons
        ps = "ps-" + secrets.token_hex(6)
        src = {"Id": it["Id"], "Protocol": "File", "Container": cont, "Bitrate": rate, "SupportsDirectPlay": direct,
               "SupportsDirectStream": direct, "SupportsTranscoding": True, "MediaStreams": []}
        for s_ in (it.get("MediaSources") or [{}])[0].get("MediaStreams", []):
            s2 = dict(s_)
            if s2.get("Type") == "Subtitle" and not direct and not s2.get("IsExternal"):
                # Embedded text subtitles leave the conversion; the server offers them as files.
                s2["DeliveryMethod"] = "External"
                s2["DeliveryUrl"] = f"/Videos/{it['Id']}/{it['Id']}/Subtitles/{s2['Index']}/0/Stream.srt?api_key={tok}"
            src["MediaStreams"].append(s2)
        if not direct:
            tp = (prof.get("TranscodingProfiles") or [{}])[0]
            qs = (f"DeviceId=mock&MediaSourceId={it['Id']}&VideoCodec={tp.get('VideoCodec', 'h264').split(',')[0]}"
                  f"&AudioCodec={tp.get('AudioCodec', 'aac').split(',')[0]}&PlaySessionId={ps}&api_key={tok}"
                  f"&TranscodeReasons={','.join(reasons)}&SegmentContainer={tp.get('Container', 'ts')}")
            src["TranscodingUrl"] = f"/videos/{it['Id']}/master.m3u8?{qs}"
            src["TranscodingSubProtocol"] = "hls"
            src["TranscodingContainer"] = tp.get("Container", "ts")
            if self.server.state.modern:
                src["TranscodeReasons"] = reasons   # 10.9+: also on the source (10.8: only in the URL)
        return self.send_json({"MediaSources": [src], "PlaySessionId": ps})

    def hls_dir(self, it):
        st = self.server.state
        with st.hls_lock:
            if it["Id"] in st.hls:
                return st.hls[it["Id"]]
            d = os.path.join(st.tmp, "hls-" + it["Id"])
            os.makedirs(d, exist_ok=True)
            src = os.path.join(st.args.media, it.get("TranscodeFrom") or it["File"])
            subprocess.run(["ffmpeg", "-v", "error", "-y", "-i", src, "-map", "0:v:0", "-map", "0:a:0?",
                            "-c:v", "libx264", "-preset", "ultrafast", "-g", "30", "-keyint_min", "30", "-sc_threshold", "0",
                            "-c:a", "aac", "-b:a", "128k", "-ac", "2", "-f", "hls", "-hls_time", "2",
                            "-hls_playlist_type", "vod", "-hls_segment_filename", os.path.join(d, "%d.ts"),
                            os.path.join(d, "main.m3u8")], check=True, timeout=120)
            st.hls[it["Id"]] = d
            return d

    def hls(self, it, what, seg, q):
        d = self.hls_dir(it)
        query = "&".join(f"{k}={v}" for k, v in q.items())
        if what.lower() == "master.m3u8":
            body = ("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=640x480,CODECS=\"avc1.42c01e,mp4a.40.2\"\n"
                    f"main.m3u8?{query}\n").encode()
            return self.send_bytes(body, "application/vnd.apple.mpegurl")
        if what.lower() == "main.m3u8":
            lines = []
            for line in open(os.path.join(d, "main.m3u8")).read().splitlines():
                m = re.fullmatch(r"(\d+)\.ts", line.strip())
                lines.append(f"hls1/main/{m.group(1)}.ts?{query}" if m else line)
            return self.send_bytes(("\n".join(lines) + "\n").encode(), "application/vnd.apple.mpegurl")
        fn = os.path.join(d, f"{int(seg)}.ts")
        if not os.path.exists(fn):
            return self.fail(404)
        return self.send_bytes(open(fn, "rb").read(), "video/mp2t")

    def send_bytes(self, data, ctype):
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        return 200

    def image(self, it):
        from PIL import Image, ImageDraw
        h = 300
        w = 200 if it["Type"] not in ("Episode",) else 356
        img = Image.new("RGB", (w, h), (40 + hash(it["Id"]) % 120, 60, 110))
        d = ImageDraw.Draw(img)
        d.rectangle([8, 8, w - 9, h - 9], outline=(242, 163, 58), width=4)
        d.text((16, h // 2 - 10), it["Name"], fill=(255, 255, 255))
        buf = io.BytesIO()
        img.save(buf, "PNG")
        data = buf.getvalue()
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)
        return 200

    def stream(self, fn):
        size = os.path.getsize(fn)
        start, end = 0, size - 1
        status = 200
        rng = self.headers.get("Range")
        if rng:
            m = re.match(r"bytes=(\d*)-(\d*)", rng)
            if m:
                if m.group(1):
                    start = int(m.group(1))
                if m.group(2):
                    end = min(int(m.group(2)), size - 1)
                status = 206
        if start >= size:
            self.send_response(416)
            self.send_header("Content-Range", f"bytes */{size}")
            self.end_headers()
            return 416
        self.send_response(status)
        self.send_header("Content-Type", mimetypes.guess_type(fn)[0] or "application/octet-stream")
        self.send_header("Accept-Ranges", "bytes")
        self.send_header("Content-Length", str(end - start + 1))
        if status == 206:
            self.send_header("Content-Range", f"bytes {start}-{end}/{size}")
        self.end_headers()
        with open(fn, "rb") as f:
            f.seek(start)
            left = end - start + 1
            while left > 0:
                chunk = f.read(min(65536, left))
                if not chunk:
                    break
                self.wfile.write(chunk)
                left -= len(chunk)
        return status

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--media", required=True)
    ap.add_argument("--port", type=int, default=18096)
    ap.add_argument("--version", default="10.10.3")
    ap.add_argument("--log", default="jellyfin-requests.jsonl")
    ap.add_argument("--tls-cert", help="serve HTTPS with this certificate (PEM)")
    ap.add_argument("--tls-key", help="private key for --tls-cert")
    args = ap.parse_args()
    srv = ThreadingHTTPServer(("127.0.0.1", args.port), Handler)
    srv.daemon_threads = True
    srv.state = State(args)
    scheme = "http"
    if args.tls_cert:
        import ssl
        ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ctx.load_cert_chain(args.tls_cert, args.tls_key)
        srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
        scheme = "https"
    print(f"mock Jellyfin {args.version} on {scheme}://127.0.0.1:{args.port}", flush=True)
    srv.serve_forever()

if __name__ == "__main__":
    main()
