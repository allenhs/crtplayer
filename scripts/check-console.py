#!/usr/bin/env python3
"""Checks the Sega CD FMV look (tests/automation/console.txt). Usage: check-console.py OUT_DIR"""
import json, os, sys
from PIL import Image

out = sys.argv[1]
fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")

LEVELS = {0, 52, 87, 116, 144, 172, 206, 255}   # the Mega Drive's eight levels per channel
log = json.load(open(os.path.join(out, 'console.json')))
rep = {e['label']: e for e in log if e['cmd'] == 'report'}
check('the script ran without a failed step', not [e for e in log if e.get('ok') is False], ', '.join(e['cmd'] for e in log if e.get('ok') is False) or 'all ok')

def img(name):
    return Image.open(os.path.join(out, name)).convert('RGB')
def colours(im):
    return set(im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata())
def blocks(im, box=None):
    """4x4 blocks of the picture as byte strings."""
    x0, y0, x1, y1 = box or (0, 0) + im.size
    return [im.crop((x, y, x + 4, y + 4)).tobytes() for y in range(y0, y1, 4) for x in range(x0, x1, 4)]

a, b = img('fmv-a.png'), img('fmv-b.png')
ca = colours(a)
check('a 4:3 video becomes the console\'s 256 × 224 screen', a.size == (256, 224), f'{a.size[0]} × {a.size[1]}')
check('at most 64 colours on screen', len(ca) <= 64 and len(ca) >= 40 and rep['sega-b']['fmvColorsUsed'] <= 64, f'{len(ca)} colours in the frame')
check('every colour is one of the console\'s 512 (3 bits per channel)', all(c in LEVELS for p in ca for c in p),
      f"levels used: {sorted({c for p in ca for c in p})}")

# frame rate: the picture changes at most 15 times a second; set to 5, five times a second
dt = (rep['sega-b']['positionMs'] - rep['sega-a']['positionMs']) / 1000
drawn = rep['sega-b']['fmvFramesDrawn'] - rep['sega-a']['fmvFramesDrawn']
shown = rep['sega-b']['framesPresented'] - rep['sega-a']['framesPresented']
check('the picture changes no more than 15 times a second', dt > 2 and drawn <= 15 * dt + 2 and drawn >= 3, f'{drawn:.0f} codec frames in {dt:.1f} s of video ({shown} video frames shown)')
dt = (rep['hold-b']['positionMs'] - rep['hold-a']['positionMs']) / 1000
drawn = rep['hold-b']['fmvFramesDrawn'] - rep['hold-a']['fmvFramesDrawn']
shown = rep['hold-b']['framesPresented'] - rep['hold-a']['framesPresented']
check('set to 5 frames a second, it changes 5 times a second while the video runs on', abs(drawn - 5 * dt) <= 2.5 and shown > drawn,
      f'{drawn:.0f} codec frames in {dt:.1f} s ({drawn / dt:.1f}/s); {shown} video frames shown')

# the codec: flat blocks, and blocks left unchanged from one frame to the next
def flat_share(im):
    bl = blocks(im)
    return sum(1 for t in bl if len({t[i:i + 3] for i in range(0, 48, 3)}) <= 2) / len(bl)
hard, nb = img('fmv-hard.png'), img('fmv-noblocks-a.png')
check('the codec flattens blocks: at full strength far more 4×4 blocks hold one or two colours', flat_share(hard) > flat_share(nb) + 0.15,
      f'{flat_share(hard) * 100:.0f}% of blocks at full strength (no dither) vs {flat_share(nb) * 100:.0f}% with the codec off')
same = sum(1 for x, y in zip(blocks(a), blocks(b)) if x == y) / len(blocks(a))
nb2 = img('fmv-noblocks-b.png')
same0 = sum(1 for x, y in zip(blocks(nb), blocks(nb2)) if x == y) / len(blocks(nb))
check("blocks that hardly change are left as they were (and the rest redrawn)", 0.08 < same < 0.97 and same > same0 + 0.10,
      f'{same * 100:.0f}% of blocks identical in the next frame, {same0 * 100:.0f}% with the codec off')
c16 = colours(img('fmv-16.png'))
check('the colour count follows the setting (16)', len(c16) <= 16 and len(c16) >= 10 and all(c in LEVELS for p in c16 for c in p), f'{len(c16)} colours')

w = img('fmv-window.png')
px = w.load()
border = all(px[x, y] == (0, 0, 0) for x in range(0, 256, 5) for y in (0, 10, 30, 223, 213, 193)) and all(px[x, y] == (0, 0, 0) for y in range(0, 224, 5) for x in (0, 10, 30, 255, 245, 225))
bbox = w.point(lambda v: 255 if v else 0).getbbox()
check('the small-window look: the video in the middle, black around it', w.size == (256, 224) and border and bbox and 140 <= bbox[2] - bbox[0] <= 176
      and 116 <= bbox[3] - bbox[1] <= 152 and abs((bbox[0] + bbox[2]) / 2 - 128) <= 6 and abs((bbox[1] + bbox[3]) / 2 - 112) <= 6,
      f'window {bbox[2] - bbox[0]} × {bbox[3] - bbox[1]} at ({bbox[0]}, {bbox[1]}) on a {w.size[0]} × {w.size[1]} screen')
check('the look brings the console\'s sound (8-bit PCM) and a clean look takes it away', rep['window']['tapeSound']['crush'] > 0.5
      and rep['window']['tapeSoundActive'] and rep['clean']['tapeSound']['crush'] == 0, f"crush {rep['window']['tapeSound']['crush']:.2f} / {rep['clean']['tapeSound']['crush']:.2f}")
wide = img('fmv-169.png')
check('a 16:9 video keeps the console\'s pixel shape: 340 × 224', wide.size == (340, 224) and len(colours(wide)) <= 64, f'{wide.size[0]} × {wide.size[1]}, {len(colours(wide))} colours')
scr = img('fmv-screen.png')
check('it goes on through the TV (the filtered screenshot shows the tube)', scr.size[0] > 600 and len(colours(scr.resize((160, 120)))) > 200, f'{scr.size[0]} × {scr.size[1]}')
print(f"\n{fails} console check(s) failed" if fails else "\nAll console checks passed")
sys.exit(1 if fails else 0)
