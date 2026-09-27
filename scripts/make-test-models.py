#!/usr/bin/env python3
"""Test models for the 90s CG room (OBJ and STL, binary and text, one broken file).
Usage: make-test-models.py OUT_DIR"""
import math, struct, sys, pathlib
out = pathlib.Path(sys.argv[1]); out.mkdir(parents=True, exist_ok=True)

def revolve(profile, seg=32):
    """A surface of revolution (Y up): list of triangles (3 points each)."""
    tris = []
    for i in range(len(profile) - 1):
        (r0, y0), (r1, y1) = profile[i], profile[i + 1]
        for j in range(seg):
            a0, a1 = 2 * math.pi * j / seg, 2 * math.pi * (j + 1) / seg
            p = lambda r, y, a: (r * math.cos(a), y, r * math.sin(a))
            tris.append((p(r0, y0, a0), p(r1, y1, a0), p(r1, y1, a1)))
            tris.append((p(r0, y0, a0), p(r1, y1, a1), p(r0, y0, a1)))
    return tris

# a bust: a pedestal foot, shoulders, a neck and a round head (OBJ, Y up, with normals)
prof = [(0.0, 0.0), (0.45, 0.0), (0.45, 0.12), (0.3, 0.2), (0.28, 0.5), (0.55, 0.7), (0.6, 0.85), (0.35, 1.0), (0.14, 1.05), (0.14, 1.15)]
prof += [(math.sin(t) * 0.28, 1.43 - math.cos(t) * 0.28) for t in [i * math.pi / 12 for i in range(13)]]
tris = revolve(prof)
with open(out / 'a_bust.obj', 'w') as f:
    f.write('# test bust\n')
    for t in tris:
        for v in t: f.write('v %.5f %.5f %.5f\n' % v)
    for t in tris:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
        ux, uy, uz, vx, vy, vz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        l = math.sqrt(nx * nx + ny * ny + nz * nz) or 1
        for _ in range(3): f.write('vn %.5f %.5f %.5f\n' % (nx / l, ny / l, nz / l))
    for i in range(len(tris)):
        a = i * 3 + 1
        f.write('f %d//%d %d//%d %d//%d\n' % (a, a, a + 1, a + 1, a + 2, a + 2))

# a twisted column (binary STL, Z up, as 3D-printing files usually are)
tw = []
n, H = 24, 40
for k in range(H):
    z0, z1 = k / H * 3.0, (k + 1) / H * 3.0
    for j in range(n):
        def pt(z, jj):
            a = 2 * math.pi * jj / n + z * 0.8
            r = 0.4 + 0.08 * math.cos(4 * 2 * math.pi * jj / n)
            return (r * math.cos(a), r * math.sin(a), z)
        tw.append((pt(z0, j), pt(z1, j), pt(z1, j + 1)))
        tw.append((pt(z0, j), pt(z1, j + 1), pt(z0, j + 1)))
with open(out / 'b_twist.stl', 'wb') as f:
    f.write(b'test twisted column'.ljust(80, b' '))
    f.write(struct.pack('<I', len(tw)))
    for t in tw:
        f.write(struct.pack('<3f', 0, 0, 0))
        for v in t: f.write(struct.pack('<3f', *v))
        f.write(b'\x00\x00')

# an obelisk (text STL, Z up)
base, top, h = 0.3, 0.18, 2.4
c = [(-base, -base, 0), (base, -base, 0), (base, base, 0), (-base, base, 0),
     (-top, -top, h), (top, -top, h), (top, top, h), (-top, top, h), (0, 0, h + 0.35)]
faces = [(0, 1, 5), (0, 5, 4), (1, 2, 6), (1, 6, 5), (2, 3, 7), (2, 7, 6), (3, 0, 4), (3, 4, 7),
         (4, 5, 8), (5, 6, 8), (6, 7, 8), (7, 4, 8), (0, 2, 1), (0, 3, 2)]
with open(out / 'c_obelisk.stl', 'w') as f:
    f.write('solid obelisk\n')
    for a, b, d in faces:
        f.write(' facet normal 0 0 0\n  outer loop\n')
        for i in (a, b, d): f.write('   vertex %.4f %.4f %.4f\n' % c[i])
        f.write('  endloop\n endfacet\n')
    f.write('endsolid obelisk\n')

# a broken file (faces refer to vertices that do not exist) and a file that is no model
(out / 'd_broken.obj').write_text('v 0 0 0\nv 1 0 0\nf 1 2 9\n')
(out / 'notes.txt').write_text('not a model\n')
print('test models written to', out)
