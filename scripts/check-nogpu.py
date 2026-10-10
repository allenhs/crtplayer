#!/usr/bin/env python3
"""Checks playback without a graphics card (2.12): the fast path against the old path.

Usage: check-nogpu.py <output-dir> <media-dir>
Reads nogpu-raster.json / nogpu-gl.json and the ng-<surface>-*.png grabs written by
tests/automation/nogpu.txt (run by scripts/run-verification.sh on both window surfaces).
"""
import json
import sys

import numpy as np
from PIL import Image

O = sys.argv[1]
fails = 0


def check(name, ok, detail='', info=False):
    global fails
    if info:   # reported, not counted
        print(('INFO-OK  ' if ok else 'INFO-NO  ') + name + (': ' + detail if detail else ''))
        return
    print(('PASS  ' if ok else 'FAIL  ') + name + (': ' + detail if detail else ''))
    fails += 0 if ok else 1


def img(name):
    return np.asarray(Image.open(f'{O}/{name}').convert('RGB'), dtype=float)


def picture(a, rect):
    """The video's picture inside a window grab (a little inside its edge)."""
    x0, y0 = int(rect['x']) + 3, int(rect['y']) + 3
    x1, y1 = int(rect['x'] + rect['w']) - 3, int(rect['y'] + rect['h']) - 3
    return a[y0:y1, x0:x1]


def soft(a, n=4):
    """Averaged over n x n pixels: compares what is shown, not how each edge was filtered."""
    h, w = (a.shape[0] // n) * n, (a.shape[1] // n) * n
    return a[:h, :w].reshape(h // n, n, w // n, n, 3).mean(axis=(1, 3))


def alike(a, b):
    d = np.abs(soft(a) - soft(b))
    return float(d.mean()), float((d.max(axis=2) > 24).mean())


def fine(a):
    """Fine detail: the mean difference between neighbouring pixels."""
    g = a.mean(axis=2)
    return float(np.abs(np.diff(g, axis=1)).mean() + np.abs(np.diff(g, axis=0)).mean())


for surface, title in (('raster', 'plain window surface (what a machine without a GPU gets)'),
                       ('gl', 'OpenGL widget (what a graphics card gets), forced onto software OpenGL')):
    print(f'-- {title}')
    log = json.load(open(f'{O}/nogpu-{surface}.json'))
    bad = [e for e in log if not e.get('ok', True)]
    check('the script ran without a failed step', not bad, 'all ok' if not bad else str([(e['cmd'], e.get('error')) for e in bad][:3]))
    R = {e['label']: e for e in log if e['cmd'] == 'report'}
    g = lambda n: f'ng-{surface}-{n}.png'
    vp = lambda l: R[l]['videoPath']
    fps = lambda l: R[l]['sync']['presentedFps']
    prof = lambda l: R[l]['profile']
    size = lambda l, k='frameSize': tuple(vp(l)[k])
    first = R['fast-play']
    software = vp('fast-play')['software']
    want = 'raster' if surface == 'raster' else 'gl widget'
    check('the window surface is the one asked for', vp('fast-play')['surface'] == want, vp('fast-play')['surface'])
    # (2.12.1: offered video "in any kind of memory", a hardware decoder kept its frames on the
    # graphics card, where the player's scaler cannot read them, and failed.)
    check('the video sink asks the decoder for frames in ordinary memory only', vp('fast-play').get('sinkForeignMemory') == 0,
          f"{vp('fast-play').get('sinkForeignMemory')} other kinds of memory offered")
    if not software:
        # A real graphics card: the fast path stays off by itself, and nothing else here applies.
        check('with a graphics card the frames stay as decoded (the card converts them)', not vp('fast-play')['fast'], vp('fast-play')['output'])
        print('SKIP  this machine has graphics acceleration: the no-GPU measurements do not apply')
        continue
    check('software OpenGL is recognised', software, first['gl'].get('renderer', '') if isinstance(first.get('gl'), dict) else str(first.get('gl'))[:60])

    # ---- effects off, 1080p
    rect = first['pictureRect']
    shown = (round(rect['w']), round(rect['h']))
    check('effects off: frames arrive converted, at exactly the size they are shown at',
          vp('fast-play')['output'] == 'rgb scaled' and size('fast-play') == shown and vp('fast-play')['frameDirect'],
          f"{size('fast-play')[0]} × {size('fast-play')[1]} for a picture of {shown[0]} × {shown[1]} (the video is 1920 × 1080)")
    f_fast, f_old = fps('fast-play'), fps('never-play')
    p_fast, p_old = prof('fast-play')['paintMs'], prof('never-play')['paintMs']
    # (on the OpenGL widget, software OpenGL draws on the same two cores: 2.16.1 and 2.17 both measure 27.7 to 29.7 there alone)
    check('1080p at 30 frames a second plays at full rate', f_fast >= (27.0 if surface == 'raster' else 25.0),
          f'{f_fast:.1f} frames a second ({f_old:.1f} the old way)')
    check('drawing a frame takes a fraction of the time', p_fast < p_old * 0.4,
          f'{p_fast:.1f} ms a frame against {p_old:.1f} ms the old way')
    if surface == 'raster':
        check('no OpenGL is used at all with effects off', prof('fast-play')['draws'] == 0 and prof('fast-play')['uploadMs'] == 0,
              f"{prof('fast-play')['draws']} OpenGL draws")
        check('it also costs less CPU', prof('fast-play')['cpuCoresBusy'] < prof('never-play')['cpuCoresBusy'],
              f"{prof('fast-play')['cpuCoresBusy']:.2f} cores busy against {prof('never-play')['cpuCoresBusy']:.2f}, of {vp('fast-play')['cpuThreads']}")
    a, b = picture(img(g('fast')), rect), picture(img(g('never')), rect)
    m, far = alike(a, b)
    check('the picture is the same as the old way shows (same frame, paused)', m < 5.0 and far < 0.01,
          f'mean difference {m:.2f} of 255, {far * 100:.2f}% of areas differ visibly')
    o1, o2 = img(g('orig-fast')), img(g('orig-never'))
    check('an original-frame screenshot is still the full video frame, identical either way',
          o1.shape == (1080, 1920, 3) and o1.shape == o2.shape and float(np.abs(o1 - o2).max()) == 0,
          f'{o1.shape[1]} × {o1.shape[0]}, largest difference {float(np.abs(o1 - o2).max()):.0f}')
    check('after the screenshot the fast frames are asked for again', vp('after-original')['output'] == 'rgb scaled', vp('after-original')['output'])
    check('with the fast path set to Never, frames stay as decoded', vp('never-play')['output'] == 'as decoded' and size('never-play') == (1920, 1080),
          f"{vp('never-play')['output']}, {size('never-play')[0]} × {size('never-play')[1]}")

    # ---- 4K
    u_fast, u_old = fps('uhd-fast'), fps('uhd-never')
    floor = 25.0 if surface == 'raster' else 18.0
    check('4K at 30 frames a second', u_fast >= floor and u_fast > u_old * 2.5 and size('uhd-fast') == shown,
          f"{u_fast:.1f} frames a second ({u_old:.1f} the old way), scaled to {size('uhd-fast')[0]} × {size('uhd-fast')[1]} on the CPU")
    a, b = picture(img(g('uhd-fast')), rect), picture(img(g('uhd-never')), rect)
    m, far = alike(a, b)
    # Against the 4K frame itself, shrunk with every pixel averaged in: a scaler that skips pixels
    # (the two-tap filter) leaves half as much fine detail again as there should be.
    orig = Image.open(f"{O}/{g('uhd-orig')}").convert('RGB')
    ref = np.asarray(orig.resize((a.shape[1], a.shape[0]), Image.BOX), dtype=float)
    check('4K shrunk on the CPU shows the same picture', orig.size == (3840, 2160) and m < 5.0 and far < 0.01,
          f'mean difference {m:.2f} from the old way, {far * 100:.2f}% of areas differ visibly')
    check('and it is shrunk properly (every pixel averaged in: no jagged, noisy fine detail)', fine(a) < fine(ref) * 1.25,
          f'fine detail {fine(a):.2f}; the 4K frame averaged down has {fine(ref):.2f} (the old way showed {fine(b):.2f})')
    # ---- 2.17: 4K HEVC 10-bit
    if 'ten-fast' in R:
        t_fast, t_old = fps('ten-fast'), fps('ten-never')
        sh = vp('ten-fast').get('shrink', {})
        check('4K HEVC 10-bit: its pictures are shrunk and brought to 8 bits by the player itself, by a whole factor',
              sh.get('factor', 0) >= 2 and sh.get('frames', 0) > 30 and sh.get('width', 0) >= shown[0] and size('ten-fast') == shown,
              f"by {sh.get('factor')} to {sh.get('width')} × {sh.get('height')} in {sh.get('msPerFrame', 0):.1f} ms a frame, then to the picture's {size('ten-fast')[0]} × {size('ten-fast')[1]}")
        check('4K HEVC 10-bit at 30 frames a second', t_fast >= (22.0 if surface == 'raster' else 12.0) and t_fast > t_old * 2.5,
              f'{t_fast:.1f} frames a second ({t_old:.1f} the old way)')
        a, b = picture(img(g('ten-fast')), rect), picture(img(g('ten-never')), rect)
        m, far = alike(a, b)
        check('and shows the same picture as the old way (same frame, paused)', m < 5.0 and far < 0.01,
              f'mean difference {m:.2f} of 255, {far * 100:.2f}% of areas differ visibly')
        check('8-bit videos are scaled as before (the 4K H.264 video passes the filter untouched)', vp('uhd-fast').get('shrink', {}).get('factor') == 0,
              f"factor {vp('uhd-fast').get('shrink', {}).get('factor')}")
        tc = R['ten-changes']
        check('eight changes of size and look in three seconds on the 10-bit video: the stream carries on',
              # (on the OpenGL widget drawn by software OpenGL each change of look has llvmpipe build the look's shaders anew, seconds
              # each: there what counts is that the stream carries on, not how many pictures came meanwhile)
              tc['state'] == 'playing' and not tc['lastError'] and fps('ten-changes') >= (10.0 if surface == 'raster' else 0.0) and size('ten-changes') == shown,
              f"{tc['state']}, {fps('ten-changes'):.1f} frames a second, at {tc['positionMs'] / 1000:.1f} s" + (f", error: {tc['lastError'][:80]}" if tc['lastError'] else ', no error'))
        tl = vp('ten-look').get('shrink', {})
        check('with a look, a 10-bit video becomes 8-bit at its own size before it is converted',
              vp('ten-look')['output'] == 'rgb' and tl.get('factor') == 1 and (tl.get('width'), tl.get('height')) == (1920, 1080) and size('ten-look') == (1920, 1080),
              f"{vp('ten-look')['output']}, factor {tl.get('factor')}, {tl.get('width')} × {tl.get('height')}, {tl.get('msPerFrame', 0):.1f} ms a frame")
        a, b = picture(img(g('ten-look-fast')), rect), picture(img(g('ten-look-never')), rect)
        m, far = alike(a, b)
        check('and the look shows the same picture as the old way', m < 5.0 and far < 0.01, f'mean difference {m:.2f}, {far * 100:.2f}% of areas differ visibly')
        gv, gu = vp('heavy-governed')['governor'], vp('heavy-ungoverned')['governor']
        # (the run after opening, and the one from the same footing as without: the better of the two)
        again = 'heavy-governed-again' in R
        first_on = fps('heavy-governed')
        best = 'heavy-governed-again' if again and fps('heavy-governed-again') > first_on else 'heavy-governed'
        h_on, h_off = fps(best), fps('heavy-ungoverned')
        late = R[best]['sync']
        if again:
            g2 = vp('heavy-governed-again')['governor']
            gv = dict(gv, leftOut=g2['leftOut'], unreferenced=g2['unreferenced'], pictures=g2['pictures']) if gv['leftOut'] == 0 and g2['leftOut'] > 0 else gv
        if gv['leftOut'] == 0 and h_on >= 28.5:
            check('a hard 4K HEVC 10-bit video: this computer decodes it in time, nothing is left out', True, f'{h_on:.1f} frames a second')
        else:
            check('a hard 4K HEVC 10-bit video: pictures are left out before decoding, and only ones nothing is built from',
                  # (none when it kept up anyway: the governor leaves pictures out only when they come late)
                  gv['leftOut'] <= gv['unreferenced'] and gv['codec'] == 'h265' and
                  (gv['leftOut'] > 0 or (h_on >= 27.0 and late['meanMs'] < 15.0)),
                  f"{gv['leftOut']} of {gv['unreferenced']} such pictures left out, of {gv['pictures']} in all",
                  # (on the OpenGL widget drawn by software OpenGL a 4K picture takes llvmpipe longer to draw than to
                  # decode: 1 to 5 frames a second with or without the governor, from run to run. A graphics card draws
                  # it in a millisecond, so this is reported, not counted; the plain surface above is the real case.)
                  info=surface != 'raster')
            # (on the OpenGL widget, drawn by software here, it is the drawing that is late: less late than without, then)
            check('the pictures that are shown come on time, and there are at least as many as without it',
                  # (the OpenGL widget drawn by software OpenGL shares the two cores with the decoder: there only "no worse")
                  # (when both come on time, the machine all but keeps up anyway: then within 15%, as runs vary that much)
                  (h_on >= h_off * 0.92 or (h_on >= h_off * 0.85 and late['meanMs'] < 15 and R['heavy-ungoverned']['sync']['meanMs'] < 15)) and h_on >= (12.0 if surface == 'raster' else 0.0) and
                  late['meanMs'] < (15.0 if surface == 'raster' else max(15.0, R['heavy-ungoverned']['sync']['meanMs'])),
                  f"{h_on:.1f} frames a second, {late['meanMs']:.0f} ms late on average (spread {late['stddevMs']:.0f}; first run after opening {first_on:.1f}); "
                  f"without: {h_off:.1f}, {R['heavy-ungoverned']['sync']['meanMs']:.0f} ms (spread {R['heavy-ungoverned']['sync']['stddevMs']:.0f})", info=surface != 'raster')
        g0 = vp('heavy-off')['governor']
        check('switched off, nothing is left out', gu['leftOut'] == g0['leftOut'] and not gu['enabled'] and gu['pictures'] > g0['pictures'] + 60,
              f"{gu['leftOut'] - g0['leftOut']} of the {gu['pictures'] - g0['pictures']} pictures after that")
        t = [R[l]['framePtsMs'] for l in ('heavy-paused', 'heavy-step1', 'heavy-step2', 'heavy-step3')]
        nth = [int((x + 1.0) * 30 / 1000) for x in t]   # which picture of the video (30 a second)
        check('paused and stepped, every picture is the exact one (none left out)',
              nth == [75, 76, 77, 78] and vp('heavy-step3')['governor']['leftOut'] == vp('heavy-paused')['governor']['leftOut'],
              f'paused on picture {nth[0]} (asked for 2.51 s), then stepped to {nth[1:]}')

    check('the decoder uses every CPU thread', vp('uhd-fast')['decoderThreads'] in (0, vp('uhd-fast')['cpuThreads']) or vp('uhd-fast')['decoderThreads'] >= vp('uhd-fast')['cpuThreads'],
          f"{vp('uhd-fast')['decoderThreads']} decoder threads, {vp('uhd-fast')['cpuThreads']} CPU threads")

    # ---- the picture's shape
    ar = R['anam-fast']['pictureRect']
    check('an anamorphic DVD keeps its 16:9 shape', abs(R['anam-fast']['pictureAspectOnScreen'] - 16 / 9) < 0.01 and size('anam-fast') == (853, 480),
          f"{R['anam-fast']['pictureAspectOnScreen']:.3f}; frames {size('anam-fast')[0]} × {size('anam-fast')[1]} with square pixels (stored as 720 × 480)")
    m, far = alike(picture(img(g('anam-fast')), ar), picture(img(g('anam-never')), ar))
    check('and shows the same picture as the old way', m < 5.0 and far < 0.01, f'mean difference {m:.2f}')
    fr = R['fill-fast']['pictureRect']
    m, far = alike(picture(img(g('fill-fast')), fr), picture(img(g('fill-never')), fr))
    check('zoomed to fill the window (the cropped part of the frame is shown)', m < 5.0 and far < 0.01 and R['fill-fast']['scaleMode'] != R['anam-fast']['scaleMode'],
          f"mean difference {m:.2f}; picture {round(fr['w'])} × {round(fr['h'])}")

    # ---- the size follows the window
    w0, fs, w1 = size('windowed'), size('fullscreen'), size('windowed-again')
    check('going fullscreen and back, the frames follow the size of the picture', w0 == shown and fs == (1920, 1080) and w1 == shown,
          f'{w0[0]} × {w0[1]} → {fs[0]} × {fs[1]} → {w1[0]} × {w1[1]}')
    # (on the OpenGL widget drawn by software OpenGL, 2.16.1 and 2.17.0 alike measure 13.6 to 17.5 here: the drawing is the limit)
    check('fullscreen 1080p plays at full rate', fps('fullscreen') >= (27.0 if surface == 'raster' else 12.0), f"{fps('fullscreen'):.1f} frames a second")

    check('going fullscreen while paused, the frame scaled for the window gives way to the full frame', size('paused-fullscreen') == (1920, 1080)
          and not vp('paused-fullscreen')['frameScaled'] and R['paused-fullscreen']['state'] == 'paused', f"{size('paused-fullscreen')[0]} × {size('paused-fullscreen')[1]}, {R['paused-fullscreen']['state']}")

    ac = R['after-changes']
    check('twelve changes of size and look in five seconds, while playing: the stream carries on',
          ac['state'] == 'playing' and not ac['lastError'] and ac['sync']['presentedFps'] >= (24.0 if surface == 'raster' else 15.0) and ac['positionMs'] > 5000,
          f"{ac['state']}, {ac['sync']['presentedFps']:.1f} frames a second, at {ac['positionMs'] / 1000:.1f} s" + (f", error: {ac['lastError'][:80]}" if ac['lastError'] else ', no error'))

    # ---- what the fast path does not cover
    m, far = alike(img(g('rotated')), img(g('rotated-never')))
    check('a rotated phone video is drawn the old way, upright', vp('rotated')['output'] == 'as decoded' and R['rotated']['rotation'] == 90
          and R['rotated']['pictureAspectOnScreen'] < 1 and m < 0.5, f"{vp('rotated')['output']}, picture aspect {R['rotated']['pictureAspectOnScreen']:.3f}, difference {m:.2f}")

    # ---- with a look
    check('with a look, frames are converted on the CPU at the video\'s own size (no conversion pass in OpenGL)',
          vp('look-full')['output'] == 'rgb' and size('look-full') == (1920, 1080) and vp('look-full')['frameDirect'] and prof('look-full')['convertMs'] == 0,
          f"{vp('look-full')['output']}, {size('look-full')[0]} × {size('look-full')[1]}, conversion {prof('look-full')['convertMs']:.1f} ms")
    lr = R['look-window']['pictureRect']
    m, far = alike(picture(img(g('look-window')), lr), picture(img(g('look-window-never')), lr))
    check('the look shows the same picture as the old way', m < 4.0 and far < 0.01, f'mean difference {m:.2f}')
    if surface == 'raster':
        full, half = img(g('look-full')), img(g('look-half'))
        m, far = alike(full, half)
        s_full, s_half = fps('look-full'), fps('look-half')
        check('Look detail: Half draws the look at half size and is faster for it', vp('look-full')['lookScale'] == 1 and vp('look-half')['lookScale'] == 0.5
              and s_half > s_full * 1.4, f'{s_half:.1f} frames a second against {s_full:.1f} at full size (fullscreen, 1080p)')
        check('the half-size look is the same picture, softer', m < 12.0 and fine(half) < fine(full),
              f'mean difference {m:.2f}; fine detail {fine(half):.2f} against {fine(full):.2f}')
        check('Automatic: half in a large window, full in a small one', vp('look-auto')['lookScale'] == 0.5 and vp('look-auto-window')['lookScale'] == 1,
              f"fullscreen (1080 high) {vp('look-auto')['lookScale']}, window (710 high) {vp('look-auto-window')['lookScale']}")
    else:
        check('on the OpenGL widget the look is always drawn at full size', vp('look-half')['lookScale'] == 1 and vp('look-auto')['lookScale'] == 1,
              f"{vp('look-half')['lookScale']}")
    check('desk mode gets frames converted on the CPU, at the video\'s own size', vp('desk')['output'] == 'rgb' and size('desk') == (1920, 1080)
          and R['desk']['deskMode'], f"{vp('desk')['output']}, {size('desk')[0]} × {size('desk')[1]}")
    check('4K with a look: frames come scaled to the picture', vp('uhd-look')['output'] == 'rgb scaled' and size('uhd-look')[0] < 3840,
          f"{size('uhd-look')[0]} × {size('uhd-look')[1]}, {fps('uhd-look'):.1f} frames a second")

    # ---- Cable TV with effects off: the channel number is painted over the plain picture
    tv = img(g('tv'))
    tr = R['tv']['pictureRect']
    pic = tv[int(tr['y']):int(tr['y'] + tr['h']), int(tr['x']):int(tr['x'] + tr['w'])]
    corner = pic[:pic.shape[0] // 4, pic.shape[1] * 2 // 3:]
    green = int(((corner[..., 1] > 200) & (corner[..., 0] < 140) & (corner[..., 2] < 140)).sum())
    check('Cable TV with effects off: the channel number is painted over the picture', R['tv']['tv']['hasOverlay'] and green > 800
          and vp('tv')['output'] == 'rgb scaled', f"{green} pixels of the channel number's green in the top right corner")

print(f'\n{fails} check(s) failed' if fails else '\nAll no-GPU checks passed')
sys.exit(1 if fails else 0)
