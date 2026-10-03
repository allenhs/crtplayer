#!/usr/bin/env python3
"""Checks the 2.9 editing run (tests/automation/edit.txt): lossless cuts (compared with the
source by scripts/check-copy.py, ffmpeg as an independent reference) and GIF clips.
Usage: check-edit.py OUT_DIR MEDIA_DIR"""
import glob, hashlib, json, os, subprocess, sys
import numpy as np
from PIL import Image, ImageStat

out, media = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
HERE = os.path.dirname(os.path.abspath(__file__))
fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

log = json.load(open(os.path.join(out, 'edit.json')))
failed = [e['cmd'] for e in log if e.get('ok') is False or 'error' in e and e.get('cmd') not in ('cut', 'gif') and not e['cmd'].startswith(('cut', 'gif'))]
rep = {e['label']: e for e in log if e['cmd'] == 'report'}
cuts = [e for e in log if e['cmd'].split()[0] == 'cut' and 'ok' in e]
gifs = [e for e in log if e['cmd'].split()[0] == 'gif' and 'ok' in e]
check('the script ran without a failed step', not [e for e in log if e.get('ok') is False], ', '.join(e['cmd'] for e in log if e.get('ok') is False) or 'all ok')

# ---- the originals are never touched
changed = []
for line in open(os.path.join(out, 'edit-before.sha256')):
    h, name = line.split(None, 1)
    name = name.strip()
    now = hashlib.sha256(open(os.path.join(out, name), 'rb').read()).hexdigest() if os.path.exists(os.path.join(out, name)) else 'missing'
    if now != h:
        changed.append(name)
check('the original files are unchanged (SHA-256 before and after)', not changed, ', '.join(changed) or '5 files identical')
parts = glob.glob(os.path.join(out, '**', '.*.part'), recursive=True)
check('no unfinished .part files are left behind', not parts, ', '.join(parts) or 'none')

# ---- keyframe stepping (the test file has a keyframe every 2 s)
kf = [rep['kf-next']['positionMs'], rep['kf-prev1']['positionMs'], rep['kf-prev2']['positionMs']]
check('Shift+→ / Shift+← step between keyframes (3.3 s → 4 → 2 → 0)', abs(kf[0] - 4000) < 50 and abs(kf[1] - 2000) < 50 and kf[2] < 50,
      ', '.join(f'{v:.0f} ms' for v in kf))

# ---- cuts
def copycheck(src, cut):
    r = subprocess.run([sys.executable, os.path.join(HERE, 'check-copy.py'), src, cut], capture_output=True, text=True)
    try:
        return r.returncode == 0, json.loads(r.stdout)
    except ValueError:
        return False, {'error': r.stderr.strip()[-200:]}

def streams(path):
    o = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'stream=codec_type,codec_name', '-of', 'csv=p=0', path],
                       capture_output=True, text=True).stdout.split()
    return o

def fmt_duration(path):
    o = subprocess.run(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', path], capture_output=True, text=True).stdout
    try:
        return float(o.strip())
    except ValueError:
        return -1

c1, c2 = cuts[0], cuts[1]
check('A–B (3.3–7.7 s) is cut from the keyframe at or before A', c1['ok'] and abs(c1['startMs'] - 2000) < 5,
      f"starts at {c1['startMs']:.0f} ms, {c1['container']}, {os.path.basename(c1['output'])}")
ok, d = copycheck(os.path.join(out, 'edit-media', 'sd_4x3_h264.mp4'), c1['output'])
v = d.get('video', {})
check('the cut is the original, frame for frame (decoded video bit-identical, audio packets byte-identical)', ok,
      f"video frames {v.get('firstFrameIndex')}–{v.get('lastFrameIndex')} of the source, audio packets "
      f"{d.get('audio', {}).get('packetsCopied')}/{d.get('audio', {}).get('packets')}")
check('it ends at B: every frame up to 7.7 s, nothing much after', v.get('lastFrameIndex', 0) >= 231 and v.get('lastFrameIndex', 0) <= 237,
      f"last frame {v.get('lastFrameIndex')} = {v.get('lastFrameIndex', 0) / 30:.2f} s (B = 7.70 s = frame 231)")
check('cutting the same section again makes a new file, never overwriting', c2['ok'] and c2['output'] != c1['output']
      and os.path.exists(c1['output']) and ' (2).' in c2['output'], os.path.basename(c2['output']))

mk = cuts[2]
st = streams(mk['output']) if mk['ok'] else []
check('Matroska: every track is kept (video, 2 audio, 2 subtitles)', mk['ok'] and len(st) == 5 and not mk['dropped'],
      f"{len(st)} streams: {' '.join(st)}")
ok, d = copycheck(os.path.join(out, 'edit-media', 'hd_16x9_multitrack.mkv'), mk['output'])
check('… a lossless copy', ok, json.dumps({k: v.get('ok') for k, v in d.items()}))
srt = subprocess.run(['ffmpeg', '-v', 'error', '-i', mk['output'], '-map', '0:s:0', '-f', 'srt', '-'], capture_output=True, text=True).stdout
dur = fmt_duration(mk['output'])
span = (mk['stopMs'] - mk['startMs']) / 1000
check('… its subtitles are there, timed from the cut, and end at B', 'Second English line' in srt and '00:00:05,0' in srt and dur <= span + 0.6,
      f"cue at {srt.split(chr(10))[1] if srt.count(chr(10)) > 1 else '-'}; file {dur:.2f} s for a {span:.2f} s section")
for e, src, name in ((cuts[3], 'ultrawide_64x27_vp9.webm', 'WebM (VP9 + Opus)'), (cuts[4], 'anamorphic_dvd_mpeg2.mkv', 'MPEG-2 + AC-3 in Matroska')):
    ok, d = copycheck(os.path.join(out, 'edit-media', src), e['output']) if e['ok'] else (False, {})
    check(f'{name}: cut losslessly, in its own container', e['ok'] and ok and e['output'].endswith(os.path.splitext(src)[1]),
          f"{os.path.basename(e['output'])}: " + json.dumps({k: v.get('ok') for k, v in d.items()}))
ro = cuts[5]
check('a video in a read-only folder is cut into ~/Videos instead', ro['ok'] and ro['output'].startswith(os.path.join(out, 'edit-home', 'Videos'))
      and not glob.glob(os.path.join(out, 'edit-ro', '*cut*')), ro['output'].replace(out, '…') or ro.get('error'))

# ---- GIFs
def gifinfo(path):
    im = Image.open(path)
    frames, total = 0, 0
    try:
        while True:
            total += im.info.get('duration', 0)
            frames += 1
            im.seek(im.tell() + 1)
    except EOFError:
        pass
    im.seek(0)
    return im, frames, total / 1000.0

def corners_dark(im):
    """How many of the frame's corners are dark: the CRT look curves the screen, so its
    corners fall outside the picture; the original picture is colour bars to the edges."""
    g = im.convert('L')
    w, h = g.size
    n = 0
    for cx, cy in ((2, 2), (w - 3, 2), (2, h - 3), (w - 3, h - 3)):
        vals = [g.getpixel((min(max(cx + dx, 0), w - 1), min(max(cy + dy, 0), h - 1))) for dx in (-1, 0, 1) for dy in (-1, 0, 1)]
        n += sum(vals) / 9 < 40
    return n

def texture(im):
    """Mean difference between neighbouring pixels inside a flat colour bar: scanlines and
    the shadow mask texture it; the original is flat."""
    g = im.convert('L')
    w, h = g.size
    x0, x1, y0, y1 = int(w * 0.02) + 4, int(w * 0.10), int(h * 0.3), int(h * 0.7)   # inside the first bar
    d = n = 0
    for y in range(y0, y1):
        for x in range(x0, x1):
            d += abs(g.getpixel((x, y)) - g.getpixel((x, y + 1)))
            n += 1
    return d / max(1, n)

def scanlines(im):
    """How many scanlines the picture shows over its full height, from the strongest fine
    ripple in the brightness down a narrow strip (the slow shading of the tube is ignored)."""
    a = np.asarray(im.convert('L'), dtype=float)
    h, w = a.shape
    prof = a[int(h * .15):int(h * .85), int(w * .45):int(w * .50)].mean(axis=1)
    f = np.abs(np.fft.rfft(prof - prof.mean()))
    f[:20] = 0
    return f.argmax() / len(prof) * h

g = {os.path.basename(e['path']): e for e in gifs}
look, orig, five, desk = (g.get(n) for n in ('gif-look.gif', 'gif-original.gif', 'gif-5s.gif', 'gif-desk.gif'))
if look and look['ok']:
    im, n, secs = gifinfo(look['path'])
    check('A–B (2–5 s) is saved as an animated GIF at the chosen size and frame rate', im.size == (480, 360) and n == 45 and abs(secs - 3.0) < 0.02,
          f"{im.size[0]}×{im.size[1]}, {n} frames (15 a second, exactly), plays {secs:.2f} s, {look['bytes'] / 1e6:.2f} MB; made in {look['tookSeconds']:.1f} s")
    check('it loops', im.info.get('loop') == 0, f"loop={im.info.get('loop')}")
    look_im = im.convert('RGB')
else:
    check('A–B GIF', False, look.get('error') if look else 'missing'); look_im = None
if orig and orig['ok']:
    im, n, secs = gifinfo(orig['path'])
    orig_im = im.convert('RGB')
    check('the original picture option: 320 wide, 10 frames a second', im.size == (320, 240) and n == 30 and abs(secs - 3.0) < 0.02,
          f"{im.size[0]}×{im.size[1]}, {n} frames, {secs:.2f} s")
    if look_im is not None:
        lcn, ocn, lt, ot = corners_dark(look_im), corners_dark(orig_im), texture(look_im), texture(orig_im)
        check('with the look the GIF shows the CRT (curved screen, textured picture); without it, the plain picture',
              lcn >= 3 and ocn == 0 and lt > 2 * max(ot, 0.3),
              f"dark corners {lcn} vs {ocn}; texture {lt:.1f} vs {ot:.1f}")
else:
    check('original-picture GIF', False, orig.get('error') if orig else 'missing')
if five and five['ok']:
    im, n, secs = gifinfo(five['path'])
    check('without A–B: the next 5 seconds', n == 75 and abs(secs - 5.0) < 0.02 and abs(five['startMs'] - 10000) < 120, f"from {five['startMs']:.0f} ms, {n} frames, plays {secs:.2f} s")
else:
    check('5-second GIF', False, five.get('error') if five else 'missing')

# ---- 2.10: Full HD and 4K
fhd, uhd, wide, deskhd = (g.get(n) for n in ('gif-fhd.gif', 'gif-4k.gif', 'gif-4k-wide.gif', 'gif-desk-hd.gif'))
if fhd and fhd['ok'] and uhd and uhd['ok']:
    fim, fn, fsecs = gifinfo(fhd['path'])
    uim, un, usecs = gifinfo(uhd['path'])
    check('Full HD: a 4:3 picture is 1440×1080', fim.size == (1440, 1080) and fn == 10 and abs(fsecs - 1.0) < 0.02,
          f"{fim.size[0]}×{fim.size[1]}, {fn} frames, {fsecs:.2f} s, {fhd['bytes'] / 1e6:.1f} MB; made in {fhd['tookSeconds']:.1f} s")
    check('4K: a 4:3 picture is 2880×2160', uim.size == (2880, 2160) and un == 10 and abs(usecs - 1.0) < 0.02,
          f"{uim.size[0]}×{uim.size[1]}, {un} frames, {usecs:.2f} s, {uhd['bytes'] / 1e6:.1f} MB; made in {uhd['tookSeconds']:.1f} s")
    fl, ul = scanlines(fim), scanlines(uim)
    # The video has 480 lines. A tube 2160 pixels tall has room to show every one of them
    # (4.5 pixels each); enlarging a smaller picture could not add them.
    check('the 4K GIF is drawn at 4K, not enlarged: the tube shows all of the video\'s 480 scanlines', 440 < ul < 500 and ul > 1.8 * fl,
          f"{ul:.0f} scanlines counted at 4K ({2160 / ul:.1f} pixels each); {fl:.0f} at Full HD, where 480 would not fit")
    after = rep['after-big']
    check('afterwards the player is as it was: paused, same look, A–B still set', after['state'] == 'paused' and after['loopAMs'] == 2000 and after['loopBMs'] == 3000
          and after['preset'] == 'Clean Broadcast Monitor',
          f"{after['state']}, {after['preset']}, A–B {after['loopAMs']:.0f}–{after['loopBMs']:.0f} ms")
else:
    check('Full HD and 4K GIFs', False, (fhd or {}).get('error') or (uhd or {}).get('error') or 'missing')
if wide and wide['ok']:
    im, n, secs = gifinfo(wide['path'])
    check('4K: a 16:9 video fills 3840×2160', im.size == (3840, 2160) and n == 10, f"{im.size[0]}×{im.size[1]}, {n} frames, {wide['bytes'] / 1e6:.1f} MB; made in {wide['tookSeconds']:.1f} s")
else:
    check('4K wide GIF', False, wide.get('error') if wide else 'missing')
if desk and desk['ok']:
    im, n, secs = gifinfo(desk['path'])
    ratio = im.size[0] / im.size[1]
    check('in desk mode, the whole scene is recorded', im.size[0] == 480 and ratio > 1.15 and n == 30 and abs(secs - 2.0) < 0.02, f"{im.size[0]}×{im.size[1]} (the room, not just the picture), {n} frames, {secs:.2f} s")
else:
    check('desk GIF', False, desk.get('error') if desk else 'missing')
if deskhd and deskhd['ok']:
    im, n, secs = gifinfo(deskhd['path'])
    # Drawn at that size: it has fine detail that the 480-wide GIF, enlarged, does not.
    def fine(g):
        a = np.asarray(g, dtype=float)   # (the mean step from one pixel to the next)
        return float(np.abs(np.diff(a, axis=1)).mean() + np.abs(np.diff(a, axis=0)).mean())
    d_hd = fine(im.convert('L'))
    d_up = fine(Image.open(desk['path']).convert('L').resize(im.size, Image.BICUBIC)) if desk and desk['ok'] else 0.0
    check('desk mode in HD: the scene is drawn at the GIF\'s size', im.size == (1280, 720) and n == 20 and d_hd > 1.5 * d_up,
          f"{im.size[0]}×{im.size[1]}, {n} frames; fine detail {d_hd:.2f} against {d_up:.2f} for the small GIF enlarged; made in {deskhd['tookSeconds']:.1f} s")
else:
    check('desk HD GIF', False, deskhd.get('error') if deskhd else 'missing')
print(f"\n{fails} edit check(s) failed" if fails else "\nAll edit checks passed")
sys.exit(1 if fails else 0)
