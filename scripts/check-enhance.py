#!/usr/bin/env python3
"""Checks Enhance (2.13): the upscaler against the full-size original, and generated frames
against the true in-between frame.

Usage: check-enhance.py <output-dir>
Reads enhance.json / enhance-unforced.json and the enh-*.png pictures written by
tests/automation/enhance.txt (run by scripts/run-verification.sh).
"""
import json
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


def luma(a):
    return a @ np.array([0.299, 0.587, 0.114])


def psnr(a, b):
    return float(10 * np.log10(255.0 ** 2 / max(1e-9, ((a - b) ** 2).mean())))


def detail(a):
    """Fine detail: the mean brightness difference between neighbouring pixels."""
    g = luma(a)
    return float(np.abs(np.diff(g, axis=1)).mean() + np.abs(np.diff(g, axis=0)).mean())


log = json.load(open(f'{O}/enhance.json'))
bad = [e for e in log if not e.get('ok', True)]
check('the script ran without a failed step', not bad, 'all ok' if not bad else str([(e['cmd'], e.get('args')) for e in bad][:3]))
R = {e['label']: e for e in log if e['cmd'] == 'report'}
en = lambda l: R[l]['enhance']

# ---- upscaling
T = rgb('enh-truth.png')
off, on, s0, s1 = rgb('enh-up-off.png'), rgb('enh-up-on.png'), rgb('enh-up-s0.png'), rgb('enh-up-s1.png')
check('a 960 × 540 video shown at 1920 × 1080 is upscaled to exactly that', T.shape == (1080, 1920, 3) and on.shape == T.shape
      and (en('up-on')['upscaledWidth'], en('up-on')['upscaledHeight']) == (1920, 1080),
      f"{en('up-on')['upscaledWidth']} × {en('up-on')['upscaledHeight']}")
dT = detail(T)
r_off, r_on, r0, r1 = detail(off) / dT, detail(on) / dT, detail(s0) / dT, detail(s1) / dT
check('the plain picture has lost much of the original\'s fine detail; the enhanced one has most of it back',
      r_off < 0.72 and 0.85 <= r_on <= 1.10,
      f'{r_off * 100:.0f}% of the original\'s fine detail plain, {r_on * 100:.0f}% enhanced')
p_off, p_on = psnr(luma(off), luma(T)), psnr(luma(on), luma(T))
check('and it is closer to the original, not just sharper', p_on > p_off + 0.3,
      f'{p_on:.2f} dB against {p_off:.2f} dB plain (brightness, against the full-size original)')
check('Sharpness: at 0 the picture is only rebuilt, at 1 it is sharpened strongly', r_off + 0.05 < r0 < r_on < r1,
      f'{r0 * 100:.0f}% at 0, {r_on * 100:.0f}% at 0.5, {r1 * 100:.0f}% at 1')
orig = rgb('enh-up-orig.png')
check('an original-frame screenshot is the video\'s own frame, not the upscaled one', orig.shape == (540, 960, 3), f'{orig.shape[1]} × {orig.shape[0]}')
lo, lf = rgb('enh-look-on.png'), rgb('enh-look-off.png')
check('a CRT look is left alone (the same picture with the setting on and off)', lo.shape == lf.shape and float(np.abs(lo - lf).max()) == 0,
      f'largest difference {float(np.abs(lo - lf).max()):.0f}')
check('a video that is not enlarged is not touched', en('hd-window')['upscale'] and en('hd-window')['upscaledFrames'] == 0,
      f"1080p in a 1280-wide window: {en('hd-window')['upscaledFrames']:.0f} frames upscaled")

# ---- frame generation
Tm = rgb('enh-fg-truth.png')
A, B = rgb('enh-fg-a.png'), rgb('enh-fg-b.png')
mid, mix = rgb('enh-fg-mid.png'), rgb('enh-fg-mix.png')
check('the two clips are on the frames meant (30 a second: 1.000 s and 1.033 s; 60 a second: 1.017 s)',
      abs(R['pair']['framePtsMs'] - 1033.3) < 1 and abs(R['truth']['framePtsMs'] - 1016.7) < 1,
      f"{R['pair']['framePtsMs']:.1f} ms and {R['truth']['framePtsMs']:.1f} ms")
p_mid, p_mix, p_a, p_b = psnr(mid, Tm), psnr(mix, Tm), psnr(A, Tm), psnr(B, Tm)
check('a frame generated halfway between two frames is close to the true in-between frame',
      p_mid >= 30.0 and p_mid >= p_mix + 6.0 and p_mid >= max(p_a, p_b) + 8.0,
      f'{p_mid:.1f} dB; a plain mix of the two frames {p_mix:.1f} dB; the frame before {p_a:.1f} dB, after {p_b:.1f} dB')
far = float((np.abs(mid - Tm).max(axis=2) > 40).mean())
check('little of it is clearly wrong', far < 0.01, f'{far * 100:.2f}% of pixels differ by more than 40 of 255 (a plain mix: {float((np.abs(mix - Tm).max(axis=2) > 40).mean()) * 100:.1f}%)')
t0, t1, t25 = rgb('enh-fg-t0.png'), rgb('enh-fg-t1.png'), rgb('enh-fg-t25.png')
check('at phase 0 it is the frame before, at phase 1 the frame after, exactly', float(np.abs(t0 - A).max()) == 0 and float(np.abs(t1 - B).max()) == 0,
      f'largest differences {float(np.abs(t0 - A).max()):.0f} and {float(np.abs(t1 - B).max()):.0f}')
check('a quarter of the way it is nearer the frame before than the halfway frame is', psnr(t25, A) > psnr(mid, A) + 1.0 and psnr(t25, B) < psnr(mid, B),
      f'against the frame before: {psnr(t25, A):.1f} dB at a quarter, {psnr(mid, A):.1f} dB at half')
check('motion is searched on a small copy of the picture', en('cut')['motionWidth'] == 480 and en('cut')['motionHeight'] in (270, 272),
      f"{en('cut')['motionWidth']} × {en('cut')['motionHeight']} for a 960 × 540 video")
ca, cb, c3, c7 = rgb('enh-cut-a.png'), rgb('enh-cut-b.png'), rgb('enh-cut-t3.png'), rgb('enh-cut-t7.png')
check('across a cut nothing is generated: the nearer real frame is shown', float(np.abs(ca - cb).mean()) > 20
      and float(np.abs(c3 - ca).max()) == 0 and float(np.abs(c7 - cb).max()) == 0,
      f'at 0.3 the frame before (largest difference {float(np.abs(c3 - ca).max()):.0f}), at 0.7 the frame after ({float(np.abs(c7 - cb).max()):.0f}); '
      f'the two scenes differ by {float(np.abs(ca - cb).mean()):.0f} on average')
p, q = R['playing'], R['paused']
check('while playing, frames are generated between the video\'s own', en('playing')['running'] and en('playing')['framePairs'] >= 1
      and en('playing')['framesGenerated'] >= 1 and en('playing')['drawsBetween'] >= 1,
      f"{en('playing')['framePairs']:.0f} pairs of frames, {en('playing')['framesGenerated']:.0f} frames generated (software OpenGL here: far from full rate)")
check('the picture runs one frame behind, and the sound is held back to match', en('playing')['pictureLatencyMs'] == 33
      and p['everyday']['audioSinkOffsetMs'] == 33, f"{en('playing')['pictureLatencyMs']} ms; sound output delayed {p['everyday']['audioSinkOffsetMs']} ms (a 30 a second video)")
check('paused, the real frame is shown', q['state'] == 'paused' and not en('paused')['running'] and en('paused')['lastPhase'] == 1.0,
      f"phase {en('paused')['lastPhase']}")
check('it works with a look too', en('playing-look')['framesGenerated'] > en('paused')['framesGenerated'] and not R['playing-look']['bypass'],
      f"{en('playing-look')['framesGenerated'] - en('paused')['framesGenerated']:.0f} more frames generated with Clean Broadcast Monitor on")
check('a video with as many frames a second as the screen shows gets none generated', not en('sixty')['useful'] and not en('sixty')['running']
      and en('sixty')['pictureLatencyMs'] == 0 and en('sixty')['screenHz'] > 30,
      f"60 a second on a {en('sixty')['screenHz']:.0f} Hz screen: generating {en('sixty')['running']}, sound delay {en('sixty')['pictureLatencyMs']} ms")
check('not in desk mode', R['desk']['deskMode'] and not en('desk')['motion'] and en('desk')['pictureLatencyMs'] == 0,
      f"generating: {en('desk')['motion']}, sound delay {en('desk')['pictureLatencyMs']} ms")
check('switched off, the sound is not held back', not en('motion-off')['motion'] and en('motion-off')['pictureLatencyMs'] == 0
      and R['motion-off']['everyday']['audioSinkOffsetMs'] == 0, f"{R['motion-off']['everyday']['audioSinkOffsetMs']} ms")

# ---- without a graphics card (not forced)
U = {e['label']: e for e in json.load(open(f'{O}/enhance-unforced.json')) if e['cmd'] == 'report'}['unforced']
ue = U['enhance']
un = rgb('enh-unforced.png')
check('without a graphics card the enhancements stay off, whatever the settings say',
      U['videoPath']['software'] and not ue['available'] and ue['upscaledFrames'] == 0 and ue['pictureLatencyMs'] == 0 and not ue['running']
      and float(np.abs(un - off).max()) == 0,
      f"available: {ue['available']}; the picture is the plain one (largest difference {float(np.abs(un - off).max()):.0f})")

print(f'\n{fails} check(s) failed' if fails else '\nAll Enhance checks passed')
sys.exit(1 if fails else 0)
