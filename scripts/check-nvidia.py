#!/usr/bin/env python3
"""Checks NVIDIA's AI methods for Enhance (2.15) as far as they can be checked without an
NVIDIA graphics card: the player, its helper program and the traffic between them are the
real ones; NVIDIA's SDK is replaced by a stand-in (tests/nvfx_mock) whose "super resolution"
is a plain enlargement and whose "frame generation" is a plain mix, and which stamps the top
left corner of everything it makes (super resolution: a red square and a blue one beside
it; frame generation: a green one below).

Usage: check-nvidia.py <output-dir>
Reads nvidia.json, nvidia-restore.json, nv-*.json and the nv-*.png pictures written by
tests/automation/nvidia*.txt (run by scripts/run-verification.sh).
"""
import json
import os
import sys

import numpy as np
from PIL import Image

O = sys.argv[1]
fails = 0


def check(name, ok, detail=''):
    global fails
    print(('PASS  ' if ok else 'FAIL  ') + name + (': ' + detail if detail else ''))
    fails += 0 if ok else 1


def rgb(name):
    return np.asarray(Image.open(f'{O}/{name}').convert('RGB'), dtype=float)


def reports(name):
    log = json.load(open(f'{O}/{name}.json'))
    return log, {e['label']: e for e in log if e['cmd'] == 'report'}


def is_colour(px, want):
    return bool(np.abs(np.asarray(px) - np.asarray(want, dtype=float)).max() <= 40)


def stamps(img, side):
    """Which stamps the picture's top left corner carries: (super resolution, frame generation)."""
    h = side // 2
    sr = is_colour(img[h, h], (255, 0, 0)) and is_colour(img[h, side + h], (0, 0, 255))
    fg = is_colour(img[side + h, h], (0, 255, 0))
    return sr, fg


log, R = reports('nvidia')
nv = lambda l: R[l]['enhance']['nvidia']
en = lambda l: R[l]['enhance']
bad = [e for e in log if not e.get('ok', True)]
check('the script ran without a failed step', not bad, 'all ok' if not bad else str([(e['cmd'], e.get('args')) for e in bad][:3]))

# ---- found and started
n = nv('up')
check('the helper program and the SDK are found, with both effects', n['usable'] and n['helper'].endswith('crtplayer-nvfx') and n['superRes'] and n['frameGen']
      and n['sdkVersion'] == '1.3.0', f"{os.path.basename(n['helper'])}, SDK {n['sdkVersion']} in {n['sdk']}")

# ---- upscaling
up, builtin, again = rgb('nv-up.png'), rgb('nv-up-builtin.png'), rgb('nv-up-again.png')
check('a 960 × 540 video shown at 1920 × 1080: the effects are opened for exactly that', n['state'] == 'ready' and n['kind'] == 2
      and (n['srcWidth'], n['srcHeight'], n['outWidth'], n['outHeight']) == (960, 540, 1920, 1080) and n['quality'] == 3 and n['mode'] == -1,
      f"{n['srcWidth']} × {n['srcHeight']} to {n['outWidth']} × {n['outHeight']}, quality {n['quality']}, no frame generation")
check('the picture on screen is the helper\'s: its stamp is in the top left corner, the right way up and in the right colours',
      up.shape == (1080, 1920, 3) and stamps(up, 54) == (True, False), f'red then blue: {stamps(up, 54)[0]}; no green below: {not stamps(up, 54)[1]}')
body = (slice(120, None), slice(None))
d_same = float(np.abs(up[body] - builtin[body]).mean())
check('away from the stamp it is the same frame as the built-in upscaler shows', d_same < 6.0, f'mean difference {d_same:.2f} of 255')
check('each frame is sent once and its picture taken once (paused: nothing is asked again for a frame already shown)',
      n['missed'] == 0 and 1 <= n['frames'] <= 6 and n['pictures'] == n['frames'], f"{n['frames']:.0f} frames sent, {n['pictures']:.0f} pictures taken")
b = nv('up-builtin')
check('switched off, the built-in upscaler is back and the helper is gone', not b['on'] and b['state'] == 'off' and b['kind'] == 0
      and stamps(builtin, 54) == (False, False) and en('up-builtin')['upscaledFrames'] > en('up')['upscaledFrames'],
      f"helper {b['state']}; no stamp; {en('up-builtin')['upscaledFrames'] - en('up')['upscaledFrames']:.0f} more frames upscaled by the built-in one")
check('switched on again, it is back', stamps(again, 54) == (True, False))
u = nv('up-ultra')
check('another quality opens the effects anew with it', u['state'] == 'ready' and u['quality'] == 4 and u['opens'] > n['opens'], f"quality {u['quality']}, opened {u['opens']:.0f} times so far")
look = rgb('nv-look.png')
check('a CRT look is left alone (no upscaling by NVIDIA\'s either)', nv('look')['kind'] == 0 and not R['look']['bypass'] and not is_colour(look[20, 20], (255, 0, 0)),
      f"kind {nv('look')['kind']}")

# ---- frame generation
p = nv('pair')
check('smooth motion with nothing to upscale: frame generation at the video\'s own size', p['state'] == 'ready' and p['kind'] == 1
      and (p['outWidth'], p['outHeight']) == (960, 540) and p['quality'] == 0 and p['mode'] == 1, f"{p['outWidth']} × {p['outHeight']}, mode {p['mode']}")
A, B = rgb('nv-fg-a.png'), rgb('nv-fg-b.png')
mid, t25, t0, t1 = rgb('nv-fg-mid.png'), rgb('nv-fg-t25.png'), rgb('nv-fg-t0.png'), rgb('nv-fg-t1.png')
body = (slice(60, None), slice(None))
d_mid = float(np.abs(mid[body] - (A[body] + B[body]) / 2).max())
d_25 = float(np.abs(t25[body] - (A[body] * 0.75 + B[body] * 0.25)).max())
check('the frames differ, and the picture between them is the helper\'s: the stand-in\'s mix of the two, with its stamp',
      float(np.abs(A - B).mean()) > 0.5 and d_mid <= 2 and stamps(mid, 27)[1], f'largest difference from the mix {d_mid:.0f}; green stamp: {stamps(mid, 27)[1]}')
check('a quarter of the way: the helper was asked for that moment, from the frame before towards the newest', d_25 <= 2, f'largest difference from a quarter mix {d_25:.0f}')
check('at 0 it is the frame before, at 1 the newest frame, exactly, and neither is generated',
      float(np.abs(t0 - A).max()) == 0 and float(np.abs(t1 - B).max()) == 0, f'largest differences {float(np.abs(t0 - A).max()):.0f} and {float(np.abs(t1 - B).max()):.0f}')
g = nv('grabbed')
check('four pictures were taken for these, two of them generated', g['betweenPictures'] - p['betweenPictures'] == 4 and g['generated'] - p['generated'] == 2,
      f"{g['betweenPictures'] - p['betweenPictures']:.0f} taken, {g['generated'] - p['generated']:.0f} generated")

# ---- playing
q = nv('playing-up')
check('playing, effects off, full screen: upscaled, and frames generated at the larger size (one set of effects for both)',
      q['kind'] == 2 and q['quality'] == 3 and q['mode'] == 1
      and (q['outWidth'], q['outHeight']) == (1920, 1080), f"{q['srcWidth']} × {q['srcHeight']} to {q['outWidth']} × {q['outHeight']}, quality {q['quality']}, mode {q['mode']}")
took = q['upscaledPictures'] - g['upscaledPictures']
check('the pictures on screen come from the helper, hardly any draw goes without', took >= 5 and q['missed'] - g['missed'] <= 3 + took * 0.1,
      f"{took:.0f} pictures taken, {q['missed'] - g['missed']:.0f} draws without (software OpenGL here: far from full rate)")
check('some of them generated', q['generated'] > g['generated'], f"{q['generated'] - g['generated']:.0f}")
pu = rgb('nv-playing-up.png')
check('a screenshot while playing shows the helper\'s picture', stamps(pu, 54)[0])
check('the picture runs one frame behind, and the sound is held back to match', en('playing-up')['pictureLatencyMs'] == 33
      and R['playing-up']['everyday']['audioSinkOffsetMs'] == 33, f"{en('playing-up')['pictureLatencyMs']} ms")
pa = rgb('nv-paused-up.png')
check('paused: the real frame, upscaled, not a generated one', R['paused-up']['state'] == 'paused' and stamps(pa, 54) == (True, False) and en('paused-up')['lastPhase'] == 1.0,
      f'stamps (upscaled, generated): {stamps(pa, 54)}')
l = nv('playing-look')
took = l['betweenPictures'] - nv('paused-up')['betweenPictures']
check('playing with a look: frames between at the video\'s size, the look drawn from them', not R['playing-look']['bypass'] and l['kind'] == 1
      and (l['outWidth'], l['outHeight']) == (960, 540) and took >= 3 and l['missed'] - nv('paused-up')['missed'] <= 3 + took * 0.1,
      f"{took:.0f} pictures taken, {l['missed'] - nv('paused-up')['missed']:.0f} draws without")
w = nv('playing-plain')
took = w['betweenPictures'] - l['betweenPictures']
check('playing in a window with upscaling off: the same', w['kind'] == 1 and took >= 10 and w['missed'] - l['missed'] <= 3 + took * 0.1,
      f"{took:.0f} pictures taken, {w['missed'] - l['missed']:.0f} draws without")
passing = w['passing'] - g['passing']
all_live = w['upscaledPictures'] + w['betweenPictures'] - nv('paused-up')['upscaledPictures'] - nv('paused-up')['betweenPictures']
check('draws that follow one another do not wait for the helper: each takes the picture asked for at the draw before',
      passing >= 0.8 * (q['upscaledPictures'] - g['upscaledPictures'] + all_live) and w['waitMs'] < 1.0,
      f"{passing:.0f} pictures taken that way while playing; {w['waitMs']:.2f} ms waited for a picture on average, the helper taking {w['pictureMs']:.2f} ms to make one")
s = nv('sixty')
check('a video with as many frames a second as the screen shows: nothing asked of the helper', not en('sixty')['useful'] and s['kind'] == 0
      and en('sixty')['pictureLatencyMs'] == 0, f"kind {s['kind']}")
pb, pg = nv('playing-builtin'), nv('playing-again')
check('switched off while playing: the built-in frame generation carries on', pb['state'] == 'off'
      and en('playing-builtin')['framesGenerated'] > en('sixty')['framesGenerated'], f"{en('playing-builtin')['framesGenerated'] - en('sixty')['framesGenerated']:.0f} frames generated by the built-in one")
check('switched on again while playing: the helper\'s again', pg['state'] == 'ready' and pg['kind'] == 1 and pg['betweenPictures'] > pb['betweenPictures'],
      f"{pg['betweenPictures'] - pb['betweenPictures']:.0f} pictures taken")
check('not in desk mode (as before)', R['desk']['deskMode'] and not en('desk')['motion'])
check('nothing failed along the way', pg['failures'] == 0 and pg['restarts'] == 0 and not pg['error'], f"{pg['failures']:.0f} failures, {pg['restarts']} restarts")
_, RR = reports('nvidia-restore')
r = RR['restored']['enhance']['nvidia']
check('the choices are kept', r['on'] and r['wantQuality'] == 2 and r['wantMode'] == 0, f"on {r['on']}, quality {r['wantQuality']}, mode {r['wantMode']}")
panel = Image.open(f'{O}/nv-panel.png')
check('the settings panel was captured (to look at)', panel.size[0] >= 1000, f'{panel.size[0]} × {panel.size[1]}')

# ---- things going wrong: the video plays on with the built-in methods
def fault(name):
    lg, rr = reports(name)
    fin = [e for e in lg if e['cmd'] == 'finished']
    # (Not the player's state at the moment of a report: the video goes round in a loop, and while it jumps back it
    # says "paused" for a moment. That frames kept coming is what counts.)
    moving = rr['playing']['framesPresented'] >= 10 and rr['end']['framesPresented'] > rr['paused']['framesPresented']
    shot = rgb(f'{name}-playing.png')
    return rr, bool(fin) and moving and float(shot.mean()) > 20, (stamps(shot, 54), stamps(rgb(f'{name}-paused.png'), 54))

rr, ok, st = fault('nv-fine')
f = rr['end']['enhance']['nvidia']
check('(the same short script with nothing wrong: the helper\'s pictures, on the card)', ok and st[0][0] and st[1] == (True, False) and f['kind'] == 2 and f['onCard'] and f['failures'] == 0,
      f"stamps {st}")
rr, ok, st = fault('nv-nocard')
f = rr['end']['enhance']['nvidia']
check('the card cannot hand a picture from one effect to the other: it goes through ordinary memory, and everything else is as before',
      ok and st[0][0] and st[1] == (True, False) and f['kind'] == 2 and not f['onCard'] and f['failures'] == 0, f"on the card: {f['onCard']}; stamps {st}")
rr, ok, st = fault('nv-slow')
f = rr['end']['enhance']['nvidia']
check('a helper that takes 25 ms longer for everything: still its pictures', ok and st[0][0] and f['kind'] == 2 and f['failures'] == 0 and f['pictures'] > 10,
      f"{f['pictures']:.0f} pictures, {f['pictureMs']:.0f} ms each")
for name, what, word in (('nv-noload', 'an effect that cannot be loaded', 'loading Video Super Resolution'), ('nv-nocreate', 'an effect that cannot be created', 'creating Video')):
    rr, ok, st = fault(name)
    f, e = rr['end']['enhance']['nvidia'], rr['end']['enhance']
    check(f'{what}: the reason is kept, it is tried three times and then left, and the built-in methods do the work',
          ok and st == ((False, False), (False, False)) and f['kind'] == 0 and word in f['error'] and f['failures'] == 3 and f['pictures'] == 0
          and e['upscaledFrames'] > 0, f"\"{f['error']}\"; {f['failures']:.0f} tries; the built-in upscaler made {e['upscaledFrames']:.0f} pictures")
rr, ok, st = fault('nv-crash')
f, e = rr['end']['enhance']['nvidia'], rr['end']['enhance']
check('the helper dying in the middle of the video: playback carries on with the built-in methods, the helper is started again a few times and then left',
      ok and f['restarts'] >= 1 and f['failures'] >= 1 and e['upscaledFrames'] > 0 and 'has stopped' in f['lastError'],
      f"\"{f['lastError']}\"; {f['restarts']} restarts, now {f['state']}; the built-in upscaler made {e['upscaledFrames']:.0f} pictures meanwhile")
rr, ok, st = fault('nv-hang')
f, e = rr['end']['enhance']['nvidia'], rr['end']['enhance']
errs = f['lastError']
stall = max(rr[k].get('maxEventLoopStallMs', 0) for k in ('playing', 'paused', 'end'))
check('the helper getting stuck: it is given a second and a half, then stopped, and playback carries on with the built-in methods',
      ok and f['restarts'] >= 1 and 'did not answer' in errs and stall < 4500, f"\"{errs}\"; {f['restarts']} restarts; longest pause of the player {stall:.0f} ms")
rr, ok, st = fault('nv-nosdk')
f = rr['end']['enhance']['nvidia']
check('no SDK installed: nothing is started, the built-in methods are used', ok and not f['usable'] and f['sdk'] == '' and f['state'] == 'off' and st == ((False, False), (False, False))
      and rr['end']['enhance']['upscaledFrames'] > 0, f"usable {f['usable']}, helper {f['state']}")
rr, ok, st = fault('nv-nohelper')
f = rr['end']['enhance']['nvidia']
check('no helper program: the same', ok and not f['usable'] and f['helper'] == '' and f['state'] == 'off' and st == ((False, False), (False, False)), f"usable {f['usable']}")
rr, ok, st = fault('nv-switchedoff')
f = rr['end']['enhance']['nvidia']
check('CRTPLAYER_NVFX_OFF: the same', ok and not f['usable'] and f['state'] == 'off' and st == ((False, False), (False, False)), f"usable {f['usable']}")
hl = open(f'{O}/nv-helper.log').read() if os.path.exists(f'{O}/nv-helper.log') else ''
check('the helper keeps a log (what was opened, what failed)', 'open 960x540 -> 1920x1080' in hl and 'FAILED' in hl, f'{len(hl.splitlines())} lines')

print(f'\n{fails} check(s) failed' if fails else '\nAll NVIDIA checks passed')
sys.exit(1 if fails else 0)
