#!/usr/bin/env python3
"""Checks tests/automation/cg.txt (the 90s CG room). Usage: check-cg.py OUT_DIR"""
import json, sys, pathlib
import numpy as np
from PIL import Image
out = pathlib.Path(sys.argv[1]); fails = 0
def check(name, ok, detail):
    global fails; fails += 0 if ok else 1; print(f"{'PASS' if ok else 'FAIL'}  {name}: {detail}")
img = lambda n: np.asarray(Image.open(out / n).convert('RGB')).astype(float)
rep = {e['label']: e for e in json.load(open(out / 'cg.json')) if e['cmd'] == 'report'}
ref, full = img('cg_ref_full.png'), img('cg_full.png')
d = np.abs(ref - full).max() if ref.shape == full.shape else 999
check('the sweeping fly-in lands on the exact fullscreen frame', d <= 2, f'max diff {d:.0f}')
ws, su, sp = img('cg_ws.png'), img('cg_sunset.png'), img('cg_space.png')
h, w, _ = ws.shape
sky = lambda a: a[:int(h * 0.12)].reshape(-1, 3).mean(axis=0)
a, b, c = sky(ws), sky(su), sky(sp)
check('palettes: teal workstation, orange sunset, dark space', a[2] > a[0] + 20 and b[0] > b[2] + 40 and c.mean() < a.mean() * 0.4,
      f'workstation {a.round(0)}, sunset {b.round(0)}, space {c.round(0)}')
floor = lambda x: x[int(h * 0.8):]
check('the neon grid floor differs from the checkerboard', np.abs(floor(img('cg_grid.png')) - floor(ws)).mean() > 25,
      f'mean difference {np.abs(floor(img("cg_grid.png")) - floor(ws)).mean():.1f}')
no = img('cg_noobj.png')
changed = (np.abs(no - ws).max(axis=2) > 40).mean()   # the objects are a small part of the view
check('the objects can be turned off', changed > 0.02, f'{changed * 100:.1f}% of the view changes')
pl = img('cg_plinth.png')
check('the plinth differs from the pedestal', np.abs(pl - ws).mean() > 2, f'mean difference {np.abs(pl - ws).mean():.1f}')
ns = img('cg_noshadow.png')
check('hard shadows (quality medium vs low)', np.abs(floor(ns) - floor(ws)).mean() > 1.0,
      f'floor difference {np.abs(floor(ns) - floor(ws)).mean():.2f}')
low, rnd = rep['cg-low'], rep['cg-round']
check('the camera stays above the floor', low['deskClearance'] > 0.1, f"{low['deskClearance']:.2f} above it (asked for pitch -40)")
check('an open world: the camera goes all the way round', abs(rnd['deskYaw'] - 170) < 0.5, f"yaw {rnd['deskYaw']:.1f}")
turn = lambda a, b: (rep[b]['deskYaw'] - rep[a]['deskYaw'] + 540) % 360 - 180
brief, seeking, idle, playing = turn('orbit-a', 'orbit-b'), turn('orbit-b', 'orbit-c'), turn('orbit-c', 'orbit-d'), turn('play-a', 'play-b')
# (real time runs ahead of the script's waits under slow rendering, so the idle time the
# player itself measured is what shows these moments were under 10 s)
ib, ic = rep['orbit-b']['cgIdleMs'], rep['orbit-c']['cgIdleMs']
sought = rep['orbit-c']['seeksNoted'] - rep['orbit-b']['seeksNoted']
check('demo-reel orbit: not while briefly paused, seeking, or playing',
      abs(brief) < 0.5 and abs(seeking) < 0.5 and abs(playing) < 0.5 and ib < 10000 and ic < 10000 and sought >= 10,
      f'paused ({ib / 1000:.1f} s idle) {brief:+.1f} deg; scrubbing while paused ({sought} seeks, then {ic / 1000:.1f} s idle) '
      f'{seeking:+.1f} deg; playing {playing:+.1f} deg')
check('demo-reel orbit once stopped and untouched for 10 s', idle > 3, f'{idle:+.1f} deg over the next 10.5 s')
red, white = img('cg_red.png'), img('cg_white.png')
# outside the set's own screen: where the red picture turns the scene red (reflections, glow)
dr = (red[..., 0] - red[..., 1]) - (white[..., 0] - white[..., 1])
scr = (red[..., 0] > 200) & (red[..., 1] < 60) & (white.mean(axis=2) > 200)   # the screen itself
reddened = (dr > 25) & ~scr
check('the picture is reflected in the scene', reddened.sum() > 800, f'{int(reddened.sum())} pixels outside the screen turn red')
rs, rd_ = rep['reveal-start'], rep['reveal-done']
check('tile reveal: starts when playback starts, and completes', rs['cgReveal'] < 0.8 and rd_['cgReveal'] == 1,
      f"{rs['cgReveal']:.2f} just after starting, then {rd_['cgReveal']:.0f}")
rv = {k: rep[k]['cgReveal'] for k in ('seek-fwd', 'seek-back', 'scrubbed', 'short-pause', 'long-pause')}
check('no tile reveal for skipping forward or back, scrubbing, or a short pause',
      all(rv[k] == 1 for k in ('seek-fwd', 'seek-back', 'scrubbed', 'short-pause')),
      ', '.join(f'{k} {v:.2f}' for k, v in rv.items() if k != 'long-pause'))
check('the tile reveal again after a long pause (10 s or more)', rv['long-pause'] < 0.8, f"{rv['long-pause']:.2f} just after resuming")
# object sets
s0 = img('cg_set0.png')
diffs = [(np.abs(img(f'cg_set{k}.png') - s0).max(axis=2) > 40).mean() for k in (1, 2, 3, 4)]
check('each object set changes the scene (toybox, organic, mannequins, mixed)', min(diffs) > 0.01, ', '.join(f'{d * 100:.1f}%' for d in diffs))
# the background crowd, and walkers that actually walk
crowd = (np.abs(img('cg_bgon.png') - img('cg_bgoff.png')).max(axis=2) > 40).mean()
check('the background crowd appears with the default set too', crowd > 0.005, f'{crowd * 100:.1f}% of the view changes')
wa, wb, wc = img('cg_walk_a.png'), img('cg_walk_b.png'), img('cg_walk_c.png')
moved = (np.abs(wa - wb).max(axis=2) > 40).mean()
check('the mannequins walk around (0.7 s later they have moved), deterministically', moved > 0.002 and np.abs(wa - wc).max() <= 1,
      f'{moved * 100:.2f}% of the view changes; same moment again: max diff {np.abs(wa - wc).max():.0f}')
# stars: small round points, not squares
st = img('cg_stars.png'); sky_ = st[:int(st.shape[0] * 0.3)].mean(axis=2)
bright = sky_ > max(60.0, np.percentile(sky_, 99.8))
from collections import deque
seen = np.zeros_like(bright); sizes, aspects = [], []
for y, x in zip(*np.nonzero(bright)):
    if seen[y, x]: continue
    q = deque([(y, x)]); seen[y, x] = True; pts = []
    while q:
        cy, cx = q.popleft(); pts.append((cy, cx))
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = cy + dy, cx + dx
            if 0 <= ny < bright.shape[0] and 0 <= nx < bright.shape[1] and bright[ny, nx] and not seen[ny, nx]:
                seen[ny, nx] = True; q.append((ny, nx))
    ys, xs = zip(*pts); sizes.append(len(pts)); aspects.append((max(xs) - min(xs) + 1) / (max(ys) - min(ys) + 1))
check('stars are small round points (not squares)', len(sizes) >= 10 and max(sizes) <= 40 and 0.5 <= float(np.median(aspects)) <= 2.0,
      f'{len(sizes)} stars, up to {max(sizes) if sizes else 0} px, median width/height {float(np.median(aspects)) if aspects else 0:.2f}')
# your 3D models
m = rep['models']
check('your models load (OBJ, binary STL, text STL); a broken file is skipped', m['modelsShown'] == 3 and any('d_broken.obj' in x for x in m['modelsSkipped']),
      f"{m['modelsShown']} shown; skipped: {'; '.join(m['modelsSkipped'])}")
nm, wm, br = img('cg_nomodels.png'), img('cg_models.png'), img('cg_bronze.png')
appear = (np.abs(wm - nm).max(axis=2) > 40).mean()
check('they appear in the scene', appear > 0.01, f'{appear * 100:.1f}% of the view changes')
lower = slice(int(h * 0.55), h)
darker = ((nm[lower].mean(axis=2) - wm[lower].mean(axis=2)) > 12).sum()
brighter = ((wm[lower].mean(axis=2) - nm[lower].mean(axis=2)) > 12).sum()
check('they cast shadows on the floor, and show in the mirror floor', darker > 500 and brighter > 500, f'{darker} floor pixels darker, {brighter} brighter')
warm = lambda a: ((a[..., 0] > a[..., 2] * 1.4) & (a[..., 0] > 60)).sum()
check('the bronze finish differs from marble', warm(br) > warm(wm) + 500, f'warm pixels {warm(wm)} (marble) -> {warm(br)} (bronze)')
alpha = np.asarray(Image.open(out / 'cg_models.png').convert('RGBA'))[..., 3]
check('the scene stays opaque (the floor mask is reset)', (alpha == 255).mean() > 0.999, f'{(alpha == 255).mean() * 100:.2f}% opaque')
check('your models folder is remembered', rep['models-again']['modelsShown'] == 3, f"{rep['models-again']['modelsShown']} shown after leaving and re-entering desk mode")
# 2.14: which way is up; PLY, GLB / glTF, FBX; point clouds; very large models
log = json.load(open(out / 'cg.json'))
info = lambda label: {x['name']: x for x in rep[label]['modelsInfo']}
ua, uy = info('up-auto')['a_zup.obj'], info('up-y')['a_zup.obj']
check('a Z-up OBJ is stood on its flat base', ua['up'] == 'Z' and ua['upWhy'] == 0 and abs(ua['height'] - 1.5) < 0.01, ua['text'])
none = img('cg_up_none.png')
def statue(name):   # the rows and columns the statue changes, above the floor's horizon line
    ch = np.abs(img(name) - none).max(axis=2) > 40
    ys, xs = np.nonzero(ch[:int(h * 0.62)])
    return (ys.min(), xs.max() - xs.min() + 1) if len(ys) else (h, 0)
ta, wa = statue('cg_up_auto.png'); ty, wy = statue('cg_up_y.png')
check('in the room it stands taller than it does lying on its back (the axis chosen wrongly by hand)',
      uy['up'] == 'Y' and uy['upWhy'] == 2 and uy['height'] < 1.2 and ta < ty - 25,
      f"top of the statue at row {ta} standing, {ty} lying; lying: {uy['text']}, {uy['height']:.2f} high")
diff = lambda a, b: int((np.abs(img(a) - img(b)).max(axis=2) > 40).sum())
still, turned = diff('cg_up_auto.png', 'cg_still_later.png'), diff('cg_turn.png', 'cg_turn_later.png')
check('statues turn slowly unless set to stand still', turned > still * 3 + 300, f'{turned} pixels change in 10 s turning, {still} standing still')
up = info('models-upright'); sk = rep['models-upright']['modelsSkipped']
check('PLY models load: text, binary little-endian, binary big-endian (triangle strips)',
      all(n in up for n in ('b_text.ply', 'c_little.ply', 'd_big.ply')) and up['b_text.ply']['triangles'] == 600 and up['c_little.ply']['triangles'] == 1280 and up['d_big.ply']['triangles'] == 1920,
      '; '.join(up[n]['text'] for n in ('b_text.ply', 'c_little.ply', 'd_big.ply') if n in up))
check('each is stood the right way up (Z-up files on their flat base; a table on its legs)',
      rep['models-upright']['modelsShown'] == 6 and up['a_zup.obj']['up'] == 'Z' and up['b_text.ply']['up'] == 'Y' and up['c_little.ply']['up'] == 'Z'
      and up['d_big.ply']['up'] == 'Y' and up['e_points.ply']['up'] == 'Z' and up['f_table.obj']['up'] == 'Z' and up['f_table.obj']['upWhy'] == 3
      and all(abs(x['height'] - 1.5) < 0.01 for n, x in up.items() if n != 'f_table.obj'),
      ', '.join(f"{n} {x['up']}" for n, x in up.items()))
pc = up['e_points.ply']
light = rep['models-upright']['modelsLight']   # without a graphics card: lighter limits
max_tris, max_points = (120000, 60000) if light else (400000, 200000)
check('a PLY of points alone is shown as a point cloud (thinned, strays dropped)',
      pc['points'] and pc['sourcePoints'] == 300040 and 2 * min(150000, max_points) - 200 <= pc['triangles'] <= 2 * min(150000, max_points) and abs(pc['width'] - pc['depth']) < 0.05,
      f"{pc['text']}; {pc['width']:.2f} wide, {pc['depth']:.2f} deep")
check('a PLY cut short is skipped, with the reason', any('a0_cut.ply' in x and 'cut short' in x for x in sk), '; '.join(sk))
wide = img('cg_wide_none.png')
def coloured(name):   # pixels the models change that are strongly coloured (their own colours, not marble)
    a = img(name); ch = np.abs(a - wide).max(axis=2) > 40
    return int((ch & ((a.max(axis=2) - a.min(axis=2)) > 90)).sum())
check('with "Their own colours" the PLY models show their colours', up['b_text.ply']['colours'] and up['c_little.ply']['colours'] and pc['colours'] and coloured('cg_upright.png') > 1500,
      f"{coloured('cg_upright.png')} strongly coloured pixels")
scn = info('models-scenes'); sk = rep['models-scenes']['modelsSkipped']
check('GLB and glTF models load (nodes, a picture inside the file, data in a file beside it)',
      all(n in scn for n in ('a_nodes.glb', 'b_textured.glb', 'c_external.gltf')) and scn['a_nodes.glb']['triangles'] == 1280 and scn['c_external.gltf']['triangles'] == 14
      and all(abs(scn[n]['height'] - 1.5) < 0.01 for n in ('a_nodes.glb', 'b_textured.glb', 'c_external.gltf')),
      '; '.join(scn[n]['text'] for n in ('a_nodes.glb', 'b_textured.glb', 'c_external.gltf') if n in scn))
check('FBX models load (text and binary), upright whichever axis the file says is up',
      all(n in scn for n in ('e_text.fbx', 'f_binary.fbx')) and scn['e_text.fbx']['triangles'] == 1280 and scn['f_binary.fbx']['triangles'] == 600
      and all(abs(scn[n]['height'] - 1.5) < 0.01 and scn[n]['up'] == 'Y' for n in ('e_text.fbx', 'f_binary.fbx')),
      '; '.join(scn[n]['text'] for n in ('e_text.fbx', 'f_binary.fbx') if n in scn))
check('they show their colours (vertex colours, pictures, materials)', all(x['colours'] for x in scn.values()) and coloured('cg_scenes.png') > 1500,
      f"{coloured('cg_scenes.png')} strongly coloured pixels")
crate = scn.get('g_crate.obj', {})
check('an OBJ with a picture (a TGA named by its material file) is divided so that the picture shows', crate.get('colours') and crate.get('sourceTriangles') == 12 and crate.get('triangles', 0) > 5000,
      f"{crate.get('text', 'not loaded')}; drawn with {crate.get('triangles', 0):,} triangles")
check('a Draco-compressed GLB is skipped, with the reason; the data file beside a glTF is not taken for a model',
      rep['models-scenes']['modelsShown'] == 6 and len(sk) == 1 and 'd_draco.glb' in sk[0] and 'Draco' in sk[0], '; '.join(sk))
lg = info('models-large')
wait = [e for e in log if e['cmd'].startswith('modelswait 120000')][0]
dn, cl = lg.get('a_dense.stl', {}), lg.get('b_cloud.ply', {})
check('a model of 1.2 million triangles is simplified, not skipped',
      dn.get('sourceTriangles') == 1202025 and max_tris * 0.4 <= dn.get('triangles', 0) <= max_tris and dn.get('up') == 'Z',
      f"{dn.get('text', 'not loaded')} (the limit here: {max_tris:,}{', without a graphics card' if light else ''})")
check('a cloud of a million points is thinned', cl.get('points') and cl.get('sourcePoints') == 1000000 and 2 * max_points - 200 <= cl.get('triangles', 0) <= 2 * max_points, cl.get('text', 'not loaded'))
check('both load in the background within seconds', wait['ok'] and wait['ms'] < 20000, f"{wait['ms'] / 1000:.1f} s")
appear = (np.abs(img('cg_large.png') - wide).max(axis=2) > 40).mean()
check('and are drawn', appear > 0.005, f'{appear * 100:.1f}% of the view changes')
kept = rep['models-kept']
check('the chosen axis and facing are kept', kept['modelsUp'] == 'Z' and kept['modelsFace'] == 3 and kept['modelsInfo'][0]['upWhy'] == 2,
      f"up {kept['modelsUp']}, facing {kept['modelsFace']}; {kept['modelsInfo'][0]['text']}")
print(f"\n{fails} CG room check(s) failed" if fails else "\nAll CG room checks passed")
sys.exit(1 if fails else 0)
