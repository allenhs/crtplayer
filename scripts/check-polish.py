#!/usr/bin/env python3
"""Checks tests/automation/polish*.txt runs (chapters, speed, loop, previews, subtitles,
resume, system report). Usage: check-polish.py OUT_DIR MEDIA_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image

out, media = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]); fails = 0
def check(name, ok, detail):
    global fails
    fails += 0 if ok else 1
    print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
rep = {}
for n in ('polish', 'polish-restore', 'polish-restore2'):
    for e in json.load(open(out / f'{n}.json')):
        if e['cmd'] == 'report': rep[e['label']] = e
pos = lambda k: rep[k]['positionMs']

check('chapter next: to the second chapter (10 s)', abs(pos('ch-next') - 10000) < 700, f"{pos('ch-next'):.0f} ms")
check('chapter next: to the third chapter (20 s)', abs(pos('ch-next2') - 20000) < 700, f"{pos('ch-next2'):.0f} ms")
check('chapter previous: back to the second chapter', abs(pos('ch-prev') - 10000) < 700, f"{pos('ch-prev'):.0f} ms")
def speed(a, b):
    return (pos(b) - pos(a)) / max(1.0, rep[b]['t'] - rep[a]['t'])
s2, s05 = speed('sp2-a', 'sp2-b'), speed('sp05-a', 'sp05-b')
check('2x speed: position advances twice as fast as real time', 1.6 < s2 < 2.4, f'{s2:.2f}x')
check('0.5x speed: position advances half as fast', 0.35 < s05 < 0.65, f'{s05:.2f}x')
l1, l2 = pos('loop1'), pos('loop2')
check('A-B loop keeps playback between 3 s and 5 s', all(2900 <= p <= 5700 for p in (l1, l2)), f'{l1:.0f} ms, {l2:.0f} ms')

t20 = np.asarray(Image.open(out / 'thumb20.png').convert('RGB')).astype(float)
t5 = np.asarray(Image.open(out / 'thumb5.png').convert('RGB')).astype(float)
ref = np.asarray(Image.open(out / 'orig20.png').convert('RGB').resize((t20.shape[1], t20.shape[0]))).astype(float)
d_match, d_other = np.abs(t20 - ref).mean(), np.abs(t5 - ref).mean()
check('seek preview is 240 px wide with the video\'s shape', t20.shape[1] == 240 and abs(t20.shape[1] / t20.shape[0] - 16 / 9) < 0.03, f'{t20.shape[1]}x{t20.shape[0]}')
check('seek preview shows the frame at the hovered time', d_match < 12 and d_other > d_match * 1.5,
      f'difference to the real 20 s frame {d_match:.1f}; the 5 s preview differs by {d_other:.1f}')

on, off, en = (np.asarray(Image.open(out / f).convert('L')).astype(float) for f in ('sub_on.png', 'sub_off.png', 'sub_en.png'))
h = on.shape[0]
def band(a, b, y0, y1): return float(np.abs(a[int(h * y0):int(h * y1)] - b[int(h * y0):int(h * y1)]).mean())
check('with subtitles on, the sidecar subtitle file is loaded by itself and drawn into the picture', band(on, off, 0.65, 1.0) > 3 * max(0.2, band(on, off, 0.0, 0.5)),
      f'bottom-third change {band(on, off, 0.65, 1.0):.2f} vs top half {band(on, off, 0.0, 0.5):.2f}')
check('another sidecar file shows different text', band(on, en, 0.65, 1.0) > 1.0 and rep['sub-en']['externalSubtitle'] == 'subs_clip.en.srt',
      f"{rep['sub-en']['externalSubtitle']}, bottom-third change {band(on, en, 0.65, 1.0):.2f}")

check('resume: the next launch continues where the file was left', 44000 <= pos('resumed') <= 49000, f"{pos('resumed'):.0f} ms (left at ~45 s)")
check('resume: a file left in its last 30 s starts from the beginning', pos('fresh') < 4000, f"{pos('fresh'):.0f} ms")
check('recent files list the played file', 'resume_clip.mp4' in rep['resumed']['recentFiles'], ', '.join(rep['resumed']['recentFiles'][:4]))

le = rep['loop-end']
check('an A-B loop that ends at the video\'s end goes round too (2.15: it used to stop there)', le['state'] == 'playing' and 400 <= le['positionMs'] <= 3000,
      f"{le['state']} at {le['positionMs']:.0f} ms, seven seconds into a loop from 0.5 s to the end of a 3 s video")
txt = (out / 'sysreport.txt').read_text()
need = ['CRT Player:', 'OS:', 'Session:', 'Qt:', 'OpenGL:', 'GStreamer:', 'Hardware decoders:', 'Current video:', 'Jellyfin:']
missing = [k for k in need if k not in txt]
check('system report covers the essentials', not missing, 'missing: ' + ', '.join(missing) if missing else f'{len(txt.splitlines())} lines')
leaks = [w for w in (str(media), 'resume_clip', '/home/', 'jellyfin.test', 'tok-') if w in txt]
check('system report contains no paths, file names or tokens', not leaks, 'found: ' + ', '.join(leaks) if leaks else 'clean')
print(f"\n{fails} polish check(s) failed" if fails else "\nAll polish checks passed")
sys.exit(1 if fails else 0)
