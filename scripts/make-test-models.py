#!/usr/bin/env python3
"""Test models for the 90s CG room.
  OUT_DIR           OBJ and STL (binary and text), one broken file
  OUT_DIR-upright   models whose up axis has to be found, and PLY in every flavour
  OUT_DIR-scenes    GLB / glTF and FBX
  OUT_DIR-large     a model and a point cloud too large to draw as they are
  OUT_DIR-single    one Z-up OBJ on its own
Usage: make-test-models.py OUT_DIR"""
import base64, io, json, math, random, struct, sys, pathlib, zlib
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

# ---------------------------------------------------------------------------------------
# 2.14: which way is up, PLY, GLB / glTF, FBX, very large models
obelisk_c, obelisk_f = list(c), list(faces)         # (the names are reused below)
def sibling(suffix):
    d = out.parent / (out.name + suffix); d.mkdir(parents=True, exist_ok=True)
    for f in d.iterdir(): f.unlink()
    return d
def indexed(tris):
    """Triangles -> (vertices, index triples), shared corners merged."""
    ids, verts, faces = {}, [], []
    for t in tris:
        f = []
        for v in t:
            k = tuple(round(c, 6) for c in v)
            if k not in ids: ids[k] = len(verts); verts.append(k)
            f.append(ids[k])
        if len(set(f)) == 3: faces.append(tuple(f))
    return verts, faces
def zup(v): return (v[0], -v[2], v[1])            # a Y-up point in a Z-up file
def normals(verts, faces):
    n = [[0.0, 0.0, 0.0] for _ in verts]
    for a, b, c in faces:
        (ax, ay, az), (bx, by, bz), (cx, cy, cz) = verts[a], verts[b], verts[c]
        ux, uy, uz, vx, vy, vz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
        f = (uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx)
        for i in (a, b, c):
            for k in range(3): n[i][k] += f[k]
    return [tuple(c / (math.sqrt(sum(x * x for x in v)) or 1) for c in v) for v in n]
def box(x0, y0, z0, x1, y1, z1):
    c = [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0), (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1)]
    q = [(0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4), (2, 3, 7, 6), (1, 2, 6, 5), (0, 4, 7, 3)]
    return [(c[a], c[b], c[d]) for a, b, d, e in q] + [(c[a], c[d], c[e]) for a, b, d, e in q]
bust_v, bust_f = indexed(tris)                    # the bust above: Y up, a flat foot
vase = [(0.0, 0.0), (0.35, 0.0), (0.4, 0.1), (0.25, 0.5), (0.2, 0.9), (0.42, 1.3), (0.5, 1.7), (0.38, 1.9), (0.3, 1.95)]
vase_v, vase_f = indexed(revolve(vase, 40))

up = sibling('-upright')
single = sibling('-single')
# a Z-up OBJ (no normals): lies on its back unless the flat foot is found
for d in (up, single):
    with open(d / 'a_zup.obj', 'w') as f:
        f.write('# the bust, Z up\n')
        for v in bust_v: f.write('v %.5f %.5f %.5f\n' % zup(v))
        for a, b, c in bust_f: f.write('f %d %d %d\n' % (a + 1, b + 1, c + 1))
# a PLY cut short (binary): named so that it is read before the sixth model
hdr = 'ply\nformat binary_little_endian 1.0\nelement vertex %d\nproperty float x\nproperty float y\nproperty float z\nelement face %d\nproperty list uchar int vertex_indices\nend_header\n' % (len(vase_v), len(vase_f))
body = b''.join(struct.pack('<3f', *v) for v in vase_v) + b''.join(struct.pack('<B3i', 3, *t) for t in vase_f)
(up / 'a0_cut.ply').write_bytes(hdr.encode() + body[:len(body) // 2])
# text PLY, Windows line ends, colours, faces of four corners (Y up)
rings = len(vase)
with open(up / 'b_text.ply', 'w', newline='') as f:
    seg = 40
    pts = [(r * math.cos(2 * math.pi * j / seg), y, r * math.sin(2 * math.pi * j / seg)) for (r, y) in vase[1:] for j in range(seg)] + [(0.0, 0.0, 0.0)]
    quads = [(i * seg + j, (i + 1) * seg + j, (i + 1) * seg + (j + 1) % seg, i * seg + (j + 1) % seg) for i in range(rings - 2) for j in range(seg)]
    foot = [(len(pts) - 1, j, (j + 1) % seg) for j in range(seg)]
    f.write('ply\r\nformat ascii 1.0\r\ncomment made by make-test-models.py\r\nobj_info a vase\r\nelement vertex %d\r\n' % len(pts))
    f.write('property float x\r\nproperty float y\r\nproperty float z\r\nproperty uchar red\r\nproperty uchar green\r\nproperty uchar blue\r\n')
    f.write('element face %d\r\nproperty list uchar int vertex_indices\r\nend_header\r\n' % (len(quads) + len(foot)))
    for (x, y, z) in pts: f.write('%.5f %.5f %.5f %d %d %d\r\n' % (x, y, z, 230, int(40 + 100 * y), 40))   # red at the foot, orange at the top
    for q in quads: f.write('4 %d %d %d %d\r\n' % q)
    for t in foot: f.write('3 %d %d %d\r\n' % t)
# binary little-endian PLY: doubles, normals, colours with alpha, properties the player has no use for; Z up
bn = normals(bust_v, bust_f)
with open(up / 'c_little.ply', 'wb') as f:
    f.write(('ply\nformat binary_little_endian 1.0\nelement vertex %d\nproperty double x\nproperty double y\nproperty double z\n'
             'property float nx\nproperty float ny\nproperty float nz\nproperty uchar red\nproperty uchar green\nproperty uchar blue\nproperty uchar alpha\n'
             'property float quality\nelement face %d\nproperty uchar flags\nproperty list uchar int vertex_indices\nproperty list uchar float texcoord\nend_header\n'
             % (len(bust_v), len(bust_f))).encode())
    for v, n in zip(bust_v, bn):
        f.write(struct.pack('<3d3f4Bf', *zup(v), *zup(n), 60, 120, min(255, int(60 + 110 * v[1])), 255, 0.5))
    for t in bust_f: f.write(struct.pack('<BB3iB6f', 0, 3, *t, 6, 0, 0, 1, 0, 1, 1))
# binary big-endian PLY: triangle strips (the twisted column, Y up, open at both ends)
n, H = 24, 40
def col(k, j):
    y = k / H * 3.0; a = 2 * math.pi * j / n + y * 0.8; r = 0.4 + 0.08 * math.cos(4 * 2 * math.pi * j / n)
    return (r * math.cos(a), y, r * math.sin(a))
cv = [col(k, j) for k in range(H + 1) for j in range(n)]
strip = []
for k in range(H):
    for j in range(n + 1): strip += [(k + 1) * n + j % n, k * n + j % n]
    strip.append(-1)
with open(up / 'd_big.ply', 'wb') as f:
    f.write(('ply\nformat binary_big_endian 1.0\nelement vertex %d\nproperty float x\nproperty float y\nproperty float z\n'
             'element tristrips 1\nproperty list int int vertex_indices\nend_header\n' % len(cv)).encode())
    for v in cv: f.write(struct.pack('>3f', *v))
    f.write(struct.pack('>i%di' % len(strip), len(strip), *strip))
# a point cloud (binary PLY, Z up, colours, no normals): 300,000 points on the bust, and a few strays far away
random.seed(7)
def area(t):
    (ax, ay, az), (bx, by, bz), (cx, cy, cz) = t
    ux, uy, uz, vx, vy, vz = bx - ax, by - ay, bz - az, cx - ax, cy - ay, cz - az
    return 0.5 * math.sqrt((uy * vz - uz * vy) ** 2 + (uz * vx - ux * vz) ** 2 + (ux * vy - uy * vx) ** 2)
def cloud(count):
    pts = []
    for t in random.choices(tris, weights=[area(t) for t in tris], k=count):
        u, v = random.random(), random.random()
        if u + v > 1: u, v = 1 - u, 1 - v
        pts.append(tuple(t[0][i] + u * (t[1][i] - t[0][i]) + v * (t[2][i] - t[0][i]) for i in range(3)))
    return pts
pts = cloud(300000)
random.shuffle(pts)
strays = [(random.uniform(-40, 40), random.uniform(-40, 40), random.uniform(-40, 40)) for _ in range(40)]
with open(up / 'e_points.ply', 'wb') as f:
    f.write(('ply\nformat binary_little_endian 1.0\nelement vertex %d\nproperty float x\nproperty float y\nproperty float z\n'
             'property uchar red\nproperty uchar green\nproperty uchar blue\nend_header\n' % (len(pts) + len(strays))).encode())
    for i, v in enumerate(pts + strays):
        f.write(struct.pack('<3f3B', *zup(v), 40, int(90 + 100 * min(1.0, max(0.0, v[1] / 1.7))), 200))
# a Z-up table (OBJ): flat on top, four legs below
table = box(-0.6, -0.4, 0.74, 0.6, 0.4, 0.8)
for lx, ly in ((-0.5, -0.3), (0.4, -0.3), (-0.5, 0.2), (0.4, 0.2)): table += box(lx, ly, 0.0, lx + 0.1, ly + 0.1, 0.74)
tv, tf = indexed(table)
with open(up / 'f_table.obj', 'w') as f:
    for v in tv: f.write('v %.4f %.4f %.4f\n' % v)
    for a, b, c in tf: f.write('f %d %d %d\n' % (a + 1, b + 1, c + 1))

# ---- GLB / glTF
sc = sibling('-scenes')
def pad4(b, fill=b'\0'): return b + fill * (-len(b) % 4)
def glb(path, doc, binary):
    j = pad4(json.dumps(doc).encode(), b' ')
    binary = pad4(binary)
    chunks = struct.pack('<II', len(j), 0x4E4F534A) + j + (struct.pack('<II', len(binary), 0x004E4942) + binary if binary else b'')
    path.write_bytes(struct.pack('<4sII', b'glTF', 2, 12 + len(chunks)) + chunks)
def views(parts):
    """[(bytes, accessor fields)] -> (buffer bytes, bufferViews, accessors)"""
    buf, bv, acc = b'', [], []
    for data, fields in parts:
        bv.append({'buffer': 0, 'byteOffset': len(buf), 'byteLength': len(data)})
        acc.append(dict(bufferView=len(bv) - 1, **fields))
        buf = pad4(buf + data)
    return buf, bv, acc
# a: the bust stored Z-up and 100 times too large, set right by its nodes (a turn, a scale, a move); colours per vertex
bz = [zup(v) for v in bust_v]
big = [(x * 100, y * 100, z * 100) for (x, y, z) in bz]
buf, bv, acc = views([
    (b''.join(struct.pack('<3f', *v) for v in big), dict(componentType=5126, count=len(big), type='VEC3', min=[min(v[i] for v in big) for i in range(3)], max=[max(v[i] for v in big) for i in range(3)])),
    (b''.join(struct.pack('<3f', *zup(n)) for n in bn), dict(componentType=5126, count=len(bn), type='VEC3')),
    (b''.join(struct.pack('<4B', 255, int(255 * min(1, v[1] / 1.7)), 30, 255) for v in bust_v), dict(componentType=5121, normalized=True, count=len(bust_v), type='VEC4')),
    (b''.join(struct.pack('<3H', *t) for t in bust_f), dict(componentType=5123, count=len(bust_f) * 3, type='SCALAR'))])
h = math.sqrt(0.5)
glb(sc / 'a_nodes.glb', {'asset': {'version': '2.0'}, 'scene': 0, 'scenes': [{'nodes': [0]}],
    'nodes': [{'children': [1], 'translation': [5, 0, 0], 'scale': [0.01, 0.01, 0.01]}, {'mesh': 0, 'rotation': [-h, 0, 0, h]}],
    'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'COLOR_0': 2}, 'indices': 3}]}],
    'buffers': [{'byteLength': len(buf)}], 'bufferViews': bv, 'accessors': acc}, buf)
# b: the vase with a picture wrapped around it (red above, blue below), stored inside the file
from PIL import Image
img = Image.new('RGB', (64, 64), (220, 30, 30))
img.paste((30, 40, 220), (0, 32, 64, 64))
png = io.BytesIO(); img.save(png, 'JPEG', quality=95); png = png.getvalue()      # (a JPEG: the usual kind inside a GLB)
vn = normals(vase_v, vase_f)
uv = [(0.5 + math.atan2(z, x) / (2 * math.pi), 1.0 - y / 1.95) for (x, y, z) in vase_v]
buf, bv, acc = views([
    (b''.join(struct.pack('<3f', *v) for v in vase_v), dict(componentType=5126, count=len(vase_v), type='VEC3')),
    (b''.join(struct.pack('<3f', *n) for n in vn), dict(componentType=5126, count=len(vn), type='VEC3')),
    (b''.join(struct.pack('<2f', *t) for t in uv), dict(componentType=5126, count=len(uv), type='VEC2')),
    (b''.join(struct.pack('<3I', *t) for t in vase_f), dict(componentType=5125, count=len(vase_f) * 3, type='SCALAR'))])
bv.append({'buffer': 0, 'byteOffset': len(buf), 'byteLength': len(png)})
buf = pad4(buf + png)
glb(sc / 'b_textured.glb', {'asset': {'version': '2.0'}, 'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0}],
    'meshes': [{'primitives': [{'attributes': {'POSITION': 0, 'NORMAL': 1, 'TEXCOORD_0': 2}, 'indices': 3, 'material': 0}]}],
    'materials': [{'pbrMetallicRoughness': {'baseColorTexture': {'index': 0}}}], 'textures': [{'source': 0}],
    'images': [{'bufferView': len(bv) - 1, 'mimeType': 'image/jpeg'}],
    'buffers': [{'byteLength': len(buf)}], 'bufferViews': bv, 'accessors': acc}, buf)
# c: text glTF with its data in a file beside it; no normals; a green material; no scene list
ob = [(x, z, -y) for (x, y, z) in obelisk_c]      # the obelisk, Y up
buf, bv, acc = views([
    (b''.join(struct.pack('<3f', *v) for v in ob), dict(componentType=5126, count=len(ob), type='VEC3')),
    (b''.join(struct.pack('<3B', *t) for t in obelisk_f), dict(componentType=5121, count=len(obelisk_f) * 3, type='SCALAR'))])
(sc / 'c obelisk data.bin').write_bytes(buf)
(sc / 'c_external.gltf').write_text(json.dumps({'asset': {'version': '2.0'}, 'nodes': [{'mesh': 0}],
    'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'indices': 1, 'material': 0}]}],
    'materials': [{'pbrMetallicRoughness': {'baseColorFactor': [0.05, 0.6, 0.1, 1]}}],
    'buffers': [{'byteLength': len(buf), 'uri': 'c%20obelisk%20data.bin'}], 'bufferViews': bv, 'accessors': acc}, indent=1))
# d: a file that says it needs Draco compression (the player says so instead of showing nothing)
glb(sc / 'd_draco.glb', {'asset': {'version': '2.0'}, 'extensionsRequired': ['KHR_draco_mesh_compression'], 'extensionsUsed': ['KHR_draco_mesh_compression'],
    'scenes': [{'nodes': [0]}], 'nodes': [{'mesh': 0}],
    'meshes': [{'primitives': [{'attributes': {'POSITION': 0}, 'extensions': {'KHR_draco_mesh_compression': {'bufferView': 0, 'attributes': {'POSITION': 0}}}}]}],
    'buffers': [{'byteLength': 4}], 'bufferViews': [{'buffer': 0, 'byteLength': 4}], 'accessors': [{'componentType': 5126, 'count': 3, 'type': 'VEC3'}]}, b'\0\0\0\0')

# g: an OBJ of 12 triangles with a material file and a TGA picture beside it (file names with spaces)
tga = bytearray(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, 64, 64, 24, 0x20))   # uncompressed, top row first
for y in range(64):
    for x in range(64): tga += bytes((30, 200, 240) if (x // 16 + y // 16) % 2 else (200, 40, 160))   # B G R: yellow and purple squares
(sc / 'g crate paint.tga').write_bytes(bytes(tga))
(sc / 'g crate.mtl').write_text('newmtl crate paint\nKd 0.8 0.8 0.8\nmap_Kd g crate paint.tga\n')
with open(sc / 'g_crate.obj', 'w') as f:
    f.write('mtllib g crate.mtl\nusemtl crate paint\n')
    cube = [(-1, 0, -1), (1, 0, -1), (1, 2, -1), (-1, 2, -1), (-1, 0, 1), (1, 0, 1), (1, 2, 1), (-1, 2, 1)]
    for v in cube: f.write('v %d %d %d\n' % v)
    f.write('vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n')
    for q in ((1, 4, 3, 2), (5, 6, 7, 8), (1, 2, 6, 5), (2, 3, 7, 6), (3, 4, 8, 7), (4, 1, 5, 8)):
        f.write('f ' + ' '.join('%d/%d' % (v, i + 1) for i, v in enumerate(q)) + '\n')

# ---- FBX: text (Z up, as 3ds Max writes it; a red material) and binary (Y up, compressed arrays)
FBX_TEXT = """; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
	Creator: "make-test-models.py"
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",2
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",1
		P: "FrontAxisSign", "int", "Integer", "",-1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
	Geometry: 1000, "Geometry::statue", "Mesh" {
		Vertices: *@NV@ {
			a: @V@
		}
		PolygonVertexIndex: *@NI@ {
			a: @I@
		}
		GeometryVersion: 124
		LayerElementMaterial: 0 {
			Version: 101
			Name: ""
			MappingInformationType: "AllSame"
			ReferenceInformationType: "IndexToDirect"
			Materials: *1 {
				a: 0
			}
		}
		Layer: 0 {
			Version: 100
			LayerElement:  {
				Type: "LayerElementMaterial"
				TypedIndex: 0
			}
		}
	}
	Model: 2000, "Model::statue", "Mesh" {
		Version: 232
		Properties70:  {
			P: "Lcl Scaling", "Lcl Scaling", "", "A",2,2,2
		}
		Shading: T
		Culling: "CullingOff"
	}
	Material: 3000, "Material::paint", "" {
		Version: 102
		ShadingModel: "lambert"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",0.8,0.1,0.1
		}
	}
}
Connections:  {
	C: "OO",2000,0
	C: "OO",1000,2000
	C: "OO",3000,2000
}
"""
def polygons(faces_):
    idx = []
    for a, b, d in faces_: idx += [a, b, -d - 1]         # the last corner of each polygon is written as -(index + 1)
    return idx
idx = polygons(bust_f)
(sc / 'e_text.fbx').write_text(FBX_TEXT.replace('@NV@', str(len(bz) * 3)).replace('@V@', ','.join('%.5f' % x for v in bz for x in v))
                               .replace('@NI@', str(len(idx))).replace('@I@', ','.join(map(str, idx))))
def fbx_prop(p):
    if isinstance(p, int): return b'I' + struct.pack('<i', p)
    if isinstance(p, float): return b'D' + struct.pack('<d', p)
    if isinstance(p, str): return b'S' + struct.pack('<I', len(p.encode())) + p.encode()
    kind, values = p                                   # ('L', n): one 64-bit number; ('d' | 'i', [...]): an array, compressed
    if kind == 'L': return b'L' + struct.pack('<q', values)
    z = zlib.compress(struct.pack('<%d%s' % (len(values), kind), *values))
    return kind.encode() + struct.pack('<III', len(values), 1, len(z)) + z
def fbx_node(node, at):
    name, props, children = node
    pb = b''.join(fbx_prop(p) for p in props)
    head = 13 + len(name)
    body = b''
    for ch in children: body += fbx_node(ch, at + head + len(pb) + len(body))
    if children or not props: body += b'\0' * 13
    return struct.pack('<IIIB', at + head + len(pb) + len(body), len(props), len(pb), len(name)) + name.encode() + pb + body
N = lambda name, props=(), children=(): (name, list(props), list(children))
P = lambda *a: N('P', a)
idx = polygons(vase_f)
nodes = [
    N('FBXHeaderExtension', (), [N('FBXHeaderVersion', (1003,)), N('FBXVersion', (7400,)), N('Creator', ('make-test-models.py',))]),
    N('GlobalSettings', (), [N('Version', (1000,)), N('Properties70', (), [
        P('UpAxis', 'int', 'Integer', '', 1), P('UpAxisSign', 'int', 'Integer', '', 1), P('FrontAxis', 'int', 'Integer', '', 2),
        P('FrontAxisSign', 'int', 'Integer', '', 1), P('CoordAxis', 'int', 'Integer', '', 0), P('CoordAxisSign', 'int', 'Integer', '', 1),
        P('UnitScaleFactor', 'double', 'Number', '', 1.0)])]),
    N('Objects', (), [
        N('Geometry', (('L', 1000), 'statue\x00\x01Geometry', 'Mesh'), [
            N('Vertices', (('d', [float(x) for v in vase_v for x in v]),)),
            N('PolygonVertexIndex', (('i', idx),)),
            N('GeometryVersion', (124,)),
            N('LayerElementMaterial', (0,), [N('Version', (101,)), N('Name', ('',)), N('MappingInformationType', ('AllSame',)),
                                             N('ReferenceInformationType', ('IndexToDirect',)), N('Materials', (('i', [0]),))]),
            N('Layer', (0,), [N('Version', (100,)), N('LayerElement', (), [N('Type', ('LayerElementMaterial',)), N('TypedIndex', (0,))])])]),
        N('Model', (('L', 2000), 'statue\x00\x01Model', 'Mesh'), [N('Version', (232,)), N('Properties70', (), [
            P('Lcl Rotation', 'Lcl Rotation', '', 'A', 0.0, 30.0, 0.0)])]),
        N('Material', (('L', 3000), 'paint\x00\x01Material', ''), [N('Version', (102,)), N('ShadingModel', ('lambert',)), N('Properties70', (), [
            P('DiffuseColor', 'Color', '', 'A', 0.1, 0.3, 0.9)])])]),
    N('Connections', (), [N('C', ('OO', ('L', 2000), ('L', 0))), N('C', ('OO', ('L', 1000), ('L', 2000))), N('C', ('OO', ('L', 3000), ('L', 2000)))])]
data = b'Kaydara FBX Binary  \x00\x1a\x00' + struct.pack('<I', 7400)
for nd in nodes: data += fbx_node(nd, len(data))
(sc / 'f_binary.fbx').write_bytes(data + b'\0' * 13)

# ---- too large to draw as they are: 1.2 million triangles (binary STL, Z up) and a million points
import numpy as np
large = sibling('-large')
rows, cols = 775, 775                                   # 2 * 775 * 775 triangles, and the foot
th = np.linspace(0.0, math.pi * 0.86, rows + 1)[:, None]   # from the top down to a cut: a flat foot closes it
ph = np.linspace(0.0, 2 * math.pi, cols + 1)[None, :]
r = 1.0 + 0.04 * np.sin(9 * ph) * np.sin(7 * th)
Pg = np.stack([r * np.sin(th) * np.cos(ph), r * np.sin(th) * np.sin(ph), np.cos(th) * np.ones_like(ph)], axis=-1)
Pg[-1, :, 2] = Pg[-1, :, 2].mean()                      # the cut is level
qa, qb, qc, qd = Pg[:-1, :-1], Pg[1:, :-1], Pg[1:, 1:], Pg[:-1, 1:]
T = np.concatenate([np.stack([qa, qb, qc], axis=2).reshape(-1, 3, 3), np.stack([qa, qc, qd], axis=2).reshape(-1, 3, 3)])
rim = Pg[-1]
centre = np.array([0.0, 0.0, rim[0, 2]])
T = np.concatenate([T, np.stack([np.broadcast_to(centre, rim[:-1].shape), rim[1:], rim[:-1]], axis=1)])
rec = np.zeros(len(T), dtype=[('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')])
rec['v'] = T
with open(large / 'a_dense.stl', 'wb') as f:
    f.write(b'dense test model'.ljust(80, b' ')); f.write(struct.pack('<I', len(T))); f.write(rec.tobytes())
rng = np.random.default_rng(3)
u = rng.normal(size=(1000000, 3)); u /= np.linalg.norm(u, axis=1)[:, None]
cl = np.zeros(len(u), dtype=[('p', '<f4', 3), ('n', '<f4', 3)])
cl['n'] = u / np.array([1.0, 1.4, 1.0]); cl['n'] /= np.linalg.norm(cl['n'], axis=1)[:, None]
cl['p'] = u * np.array([1.0, 1.4, 1.0])                 # an egg, Y up, with its normals
with open(large / 'b_cloud.ply', 'wb') as f:
    f.write(('ply\nformat binary_little_endian 1.0\nelement vertex %d\nproperty float x\nproperty float y\nproperty float z\n'
             'property float nx\nproperty float ny\nproperty float nz\nend_header\n' % len(cl)).encode())
    f.write(cl.tobytes())
print('test models written to', out, 'and', ', '.join(d.name for d in (up, sc, large, single)))
