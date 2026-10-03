#!/usr/bin/env python3
"""Checks that a cut is lossless and a stream copy (ffmpeg as an independent reference).

  - decoded: every decoded video frame of the cut is bit-identical to a contiguous run of
    the source's decoded frames (nothing re-encoded, nothing damaged at the ends). Audio is
    judged by its packets only: decoding the same AAC packets can legitimately differ (the
    first frame overlaps the one before; noise substitution (PNS) draws from a random
    generator that carries on from earlier frames);
  - packets: every audio packet of the cut is a byte-identical source packet, in order
    and contiguous. Video packets are counted too, but may legitimately differ in framing:
    the H.264/HEVC parser puts the parameter sets (SPS/PPS) into the first keyframe so the
    file starts decodable, and converts start codes when the container needs another form
    (MPEG-TS, AVI). The decoded frames are the proof for video.

Usage: check-copy.py SOURCE CUT [A_S B_S]   (prints JSON; exit 1 if not a clean copy)"""
import json, subprocess, sys

def md5s(path, decode, sel):
    cmd = ["ffmpeg", "-v", "error", "-i", path, "-map", sel]
    if not decode:
        cmd += ["-c", "copy"]
    cmd += ["-f", "framemd5", "-"]
    out = subprocess.run(cmd, capture_output=True, text=True).stdout
    rows = []
    for line in out.splitlines():
        if line.startswith("#") or not line.strip():
            continue
        f = [x.strip() for x in line.split(",")]
        rows.append((int(f[2]), f[5]))   # pts (stream time base), md5
    return rows

def run_of(src, cut):
    """Index in src where cut's hashes appear as a contiguous run, or -1. (Every candidate
    start is tried: test tones repeat, so the same frame can occur many times.)"""
    s = [h for _, h in src]
    c = [h for _, h in cut]
    for i, h in enumerate(s):
        if c and h == c[0] and s[i:i + len(c)] == c:
            return i
    return -1

src, cut = sys.argv[1], sys.argv[2]
res = {}
ok = True
def has(path, kind):
    out = subprocess.run(["ffprobe", "-v", "error", "-show_entries", "stream=codec_type", "-of", "csv=p=0", path],
                         capture_output=True, text=True).stdout.split()
    return any(tok.strip(",") == kind for tok in out)   # (a stream with side data prints "video,")

for kind, sel in (("video", "0:v:0"), ("audio", "0:a:0")):
    if not has(src, kind):
        continue
    ds, dc = md5s(src, True, sel), md5s(cut, True, sel)
    if not dc:   # the source has this track and the cut has nothing to show for it
        res[kind] = {"frames": 0, "ok": False, "missing": True}
        ok = False
        continue
    # Audio codecs overlap each frame with the one before (MDCT), so the first decoded
    # audio frame or two of any cut differ; from there on they must be identical.
    warm = 2 if kind == "audio" else 0
    start = run_of(ds, dc[warm:])
    ps, pc = md5s(src, False, sel), md5s(cut, False, sel)
    sp = [h for _, h in ps]
    body = pc[1:] if kind == "video" else pc
    found = [h in sp for _, h in body]
    # packets: a contiguous run of the source's packets (trying every start, as above)
    bh = [h for _, h in body]
    order = []
    for i, h in enumerate(sp):
        if bh and h == bh[0] and sp[i:i + len(bh)] == bh:
            order = list(range(i, i + len(bh)))
            break
    contiguous = bool(order)
    r = {"frames": len(dc), "decodedContiguous": start >= 0, "firstSourceFrame": start,
         "packets": len(pc), "packetsCopied": sum(found), "packetsInOrder": contiguous}
    if kind == "video" and start >= 0 and ds:
        # times in source frames, for the caller to compare with A/B
        r["firstFrameIndex"] = start
        r["lastFrameIndex"] = start + len(dc) - 1
        r["sourceFrames"] = len(ds)
    r["ok"] = r["decodedContiguous"] if kind == "video" else (all(found) and r["packetsInOrder"])
    ok = ok and r["ok"]
    res[kind] = r
print(json.dumps(res))
sys.exit(0 if ok and res else 1)
