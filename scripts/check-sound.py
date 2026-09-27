#!/usr/bin/env python3
"""Checks tests/automation/sound.txt (the look's sound in the player). Usage: check-sound.py OUT_DIR"""
import json, sys, pathlib
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
rep = {e['label']: e for e in json.load(open(out / 'sound.json')) if e['cmd'] == 'report'}
check('clean looks leave the sound alone', not rep['clean']['tapeSoundActive'], f"active: {rep['clean']['tapeSoundActive']}")
check('the VHS look brings tape sound', rep['vhs']['tapeSoundActive'], f"active: {rep['vhs']['tapeSoundActive']}")
adv = rep['vhs-later']['positionMs'] - rep['vhs']['positionMs']
check('playback runs on with tape sound', adv > 1500, f'{adv} ms played in 2.5 s')
v2, v0, s5, base = rep['vol200']['tapeSound'], rep['vol0']['tapeSound'], rep['str50']['tapeSound'], rep['vhs']['tapeSound']
close = lambda a, b: abs(a - b) < 1e-3
check('noise volume 200%: the hiss and crackle twice as loud, the rest unchanged',
      close(v2['noiseGain'], 2.0) and close(v2['hiss'], base['hiss']) and close(v2['speaker'], base['speaker']),
      f"gain {v2['noiseGain']:.2f}, hiss {v2['hiss']:.2f}, speaker {v2['speaker']:.2f}")
check('noise volume 0%: the noise silent', close(v0['noiseGain'], 0.0), f"gain {v0['noiseGain']:.2f}")
check('effect strength 50%: the speaker, wobble, treble loss and saturation halved, the noise unchanged',
      close(s5['wow'], base['wow'] / 2) and close(s5['speaker'], base['speaker'] / 2) and close(s5['tone'], base['tone'] / 2)
      and close(s5['saturation'], base['saturation'] / 2) and close(s5['noiseGain'], 1.0),
      f"wow {base['wow']:.3f} -> {s5['wow']:.3f}, speaker {base['speaker']:.3f} -> {s5['speaker']:.3f}")
check('the master switch turns it off', not rep['off']['tapeSoundActive'] and rep['off']['lookSound'] is False, f"active: {rep['off']['tapeSoundActive']}")
check('film prints crackle', rep['film']['tapeSoundActive'], f"active: {rep['film']['tapeSoundActive']}")
print(f"\n{fails} sound check(s) failed" if fails else "\nAll sound checks passed")
sys.exit(1 if fails else 0)
