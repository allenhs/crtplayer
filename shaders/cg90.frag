#version 330 core
// The 90s CG room ("Mind's Eye" style): an infinite mirror checkerboard (or neon grid)
// under a gradient sky, floating chrome spheres, a glass cube, a chrome ring and a marble
// column, hard sun shadows, and the TV on a pedestal, a plinth or floating. Ray traced;
// the TV takes part in reflections and shadows as a box with its picture on the front.
// Writes depth, so the real 3D set and the traced objects hide each other correctly.
// Local frame: x across, y up from the floor, z towards the viewer (the TV faces +z).
out vec4 fragColor;
uniform mat4 uInvVP;
uniform mat4 uVP;
uniform vec2 uViewport;
uniform vec3 uCamPos;
uniform vec3 uFloorPoint;    // the set's lowest point (world); the floor is uDrop below it
uniform vec3 uFloorX;
uniform vec3 uFloorNormal;
uniform vec3 uFloorZ;
uniform float uDrop;
uniform vec3 uSetMin;        // the set's box, local frame
uniform vec3 uSetMax;
uniform vec4 uScreenRect;    // the picture on the TV's front: x0, x1, y0, y1 (local)
uniform float uScreenZ;
uniform sampler2D uGlassTex;
uniform bool uHasGlass;
uniform float uGlassLod;
uniform int uPalette;        // 0 workstation, 1 sunset, 2 deep space
uniform int uFloorStyle;     // 0 checkerboard, 1 neon grid
uniform int uStand;          // 0 chrome pedestal, 1 marble plinth, 2 floating
uniform bool uObjects;
uniform int uObjectSet;      // 0 chrome & marble, 1 toybox, 2 organic, 3 mannequins, 4 mixed
uniform bool uBackground;    // a crowd in the distance: floating shapes and walking figures
uniform bool uBanding;
uniform int uBounces;        // reflections of reflections (quality)
uniform bool uShadows;
uniform float uTime;

const float kInf = 1e9;
int gKind = -1, gWhich = -1;   // the shaped object hit last (for its material)
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float hash3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
float noise2(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}

// ---- palettes -------------------------------------------------------------------------------
vec3 sunDir()
{
    if (uPalette == 1) return normalize(vec3(-0.55, 0.16, -0.82));   // low sunset sun, behind the set
    if (uPalette == 2) return normalize(vec3(0.35, 0.75, 0.25));
    return normalize(vec3(0.55, 0.62, 0.45));
}
vec3 sunColour() { return uPalette == 1 ? vec3(1.3, 0.75, 0.45) : uPalette == 2 ? vec3(0.85, 0.9, 1.1) : vec3(1.15, 1.1, 1.0); }
vec3 skyAmbient() { return uPalette == 1 ? vec3(0.28, 0.14, 0.22) : uPalette == 2 ? vec3(0.03, 0.035, 0.06) : vec3(0.10, 0.22, 0.28); }
vec3 horizonColour() { return uPalette == 1 ? vec3(1.0, 0.48, 0.20) : uPalette == 2 ? vec3(0.05, 0.06, 0.14) : vec3(0.18, 0.62, 0.66); }

vec3 sky(vec3 d)
{
    float h = clamp(d.y, -0.2, 1.0);
    vec3 top = uPalette == 1 ? vec3(0.22, 0.05, 0.32) : uPalette == 2 ? vec3(0.0, 0.0, 0.01) : vec3(0.02, 0.07, 0.22);
    vec3 c = mix(horizonColour(), top, pow(clamp(h, 0.0, 1.0), 0.55));
    float s = max(dot(d, sunDir()), 0.0);
    c += sunColour() * (pow(s, 900.0) * 6.0 + pow(s, 40.0) * 0.25);   // sun disc and its glow
    if (uPalette == 2) {
        // stars: one small round point, somewhere inside some of the cells of a sphere of
        // cells (a whole lit cell would show as a square), of varying size and brightness
        vec3 sd = d * 150.0;
        vec3 cell = floor(sd);
        if (hash3(cell) > 0.985) {
            vec3 at = vec3(hash3(cell + 1.3), hash3(cell + 2.7), hash3(cell + 5.1)) * 0.6 + 0.2;
            vec3 dv = fract(sd) - at;
            dv -= d * dot(dv, d);                                     // distance across the sky only
            float r = 0.10 + 0.14 * hash3(cell + 9.1);                 // ~2-4 pixels
            float glow = 1.0 - smoothstep(r * 0.25, r, length(dv));
            float twinkle = 0.75 + 0.25 * sin(uTime * (1.5 + 2.0 * hash3(cell + 4.4)) + hash3(cell) * 30.0);
            vec3 tint = mix(vec3(0.75, 0.82, 1.0), vec3(1.0, 0.9, 0.75), hash3(cell + 7.7));
            c += tint * glow * twinkle * (0.5 + 0.9 * hash3(cell + 3.1)) * smoothstep(0.0, 0.12, d.y);
        }
        // a ringed planet on the horizon
        vec3 pc = normalize(vec3(-0.55, 0.055, -0.83));   // low, resting on the horizon
        float pr = 0.065;
        float a = acos(clamp(dot(d, pc), -1.0, 1.0));
        if (a < pr) {
            vec3 n = normalize(d - pc * cos(a));
            float lit = clamp(dot(normalize(d - pc * 0.9), sunDir()) * 0.5 + 0.55, 0.0, 1.0);
            float bands = 0.8 + 0.2 * sin(dot(d, vec3(0.0, 1.0, 0.3)) * 90.0);
            c = vec3(0.55, 0.42, 0.62) * lit * bands;
        }
        // its ring: an ellipse around the planet
        vec3 rx = normalize(cross(pc, vec3(0.2, 1.0, 0.1))), ry = normalize(cross(rx, pc));
        vec2 rp = vec2(dot(d - pc, rx), dot(d - pc, ry) * 3.2);
        float rr = length(rp);
        if (rr > pr * 1.35 && rr < pr * 2.1 && !(a < pr && dot(d - pc, ry) > 0.0))
            c = mix(c, vec3(0.75, 0.68, 0.55), 0.55 * smoothstep(pr * 1.35, pr * 1.45, rr) * (1.0 - smoothstep(pr * 1.95, pr * 2.1, rr)));
    }
    if (d.y < 0.0) c = horizonColour() * 0.6;
    return c;
}

// ---- objects (local frame; animated) --------------------------------------------------------
vec3 setCentre() { return 0.5 * (uSetMin + uSetMax); }
// The layout's scale: 1 for every TV-sized set; a big set (the arcade cabinet) spreads the
// objects and the crowd out in proportion, so they keep clear of it and of the camera.
float layoutScale() { vec3 d = uSetMax - uSetMin; return max(1.0, max(d.z / 1.2, d.y / 2.0)); }
vec4 sphere(int i)   // centre, radius
{
    vec3 c = setCentre();
    float S = layoutScale();
    float b = sin(uTime * 0.6 + float(i) * 2.1) * 0.06;
    if (i == 0) return vec4(c.x - 1.75 * S, uDrop + (0.55 + b) * S, c.z + 0.55 * S, 0.42 * S);
    if (i == 1) return vec4(c.x + 1.85 * S, uDrop + (1.05 + b) * S, c.z - 0.25 * S, 0.30 * S);
    return vec4(c.x + 0.95 * S, uDrop + (1.95 + b) * S, c.z - 1.3 * S, 0.24 * S);
}
float hitSphere(vec3 ro, vec3 rd, vec4 s, out vec3 n)
{
    vec3 oc = ro - s.xyz;
    float b = dot(oc, rd), c = dot(oc, oc) - s.w * s.w, h = b * b - c;
    if (h < 0.0) return kInf;
    float t = -b - sqrt(h);
    if (t < 1e-3) return kInf;
    n = normalize(ro + rd * t - s.xyz);
    return t;
}
float hitBox(vec3 ro, vec3 rd, vec3 bmin, vec3 bmax, out vec3 n)
{
    vec3 inv = 1.0 / rd;
    vec3 t0 = (bmin - ro) * inv, t1 = (bmax - ro) * inv;
    vec3 tn = min(t0, t1), tf = max(t0, t1);
    float a = max(max(tn.x, tn.y), tn.z), b = min(min(tf.x, tf.y), tf.z);
    if (a > b || b < 1e-3 || a < 1e-3) return kInf;
    n = a == tn.x ? vec3(-sign(rd.x), 0, 0) : a == tn.y ? vec3(0, -sign(rd.y), 0) : vec3(0, 0, -sign(rd.z));
    return a;
}
// the glass cube, turning slowly about its vertical axis
vec3 cubeCentre() { vec3 c = setCentre(); float S = layoutScale(); return vec3(c.x - 1.05 * S, 0.32 * S, c.z + 1.25 * S); }
float cubeAngle() { return uTime * 0.25; }
float hitCube(vec3 ro, vec3 rd, out vec3 n)
{
    float a = cubeAngle(), ca = cos(a), sa = sin(a);
    mat2 r = mat2(ca, -sa, sa, ca);
    vec3 o = ro - cubeCentre();
    vec3 lo = vec3(r * o.xz, o.y).xzy, ld = vec3(r * rd.xz, rd.y).xzy;
    lo = vec3(lo.x, o.y, lo.z); ld = vec3(ld.x, rd.y, ld.z);
    vec3 ln;
    float t = hitBox(lo, ld, vec3(-0.30 * layoutScale()), vec3(0.30 * layoutScale()), ln);
    if (t >= kInf) return kInf;
    mat2 ri = mat2(ca, sa, -sa, ca);
    vec2 nxz = ri * ln.xz;
    n = normalize(vec3(nxz.x, ln.y, nxz.y));
    return t;
}
// the chrome ring (a torus), ray-marched inside its bounding sphere
vec3 torusCentre() { vec3 c = setCentre(); float S = layoutScale(); return vec3(c.x + 1.35 * S, uDrop + (1.75 + sin(uTime * 0.5) * 0.05) * S, c.z - 0.55 * S); }
vec3 torusLocal(vec3 p)
{
    float a = uTime * 0.35;
    vec3 q = p - torusCentre();
    q.xy = mat2(cos(a), -sin(a), sin(a), cos(a)) * q.xy;
    q.yz = mat2(cos(0.9), -sin(0.9), sin(0.9), cos(0.9)) * q.yz;
    return q;
}
float sdTorus(vec3 p) { vec3 q = torusLocal(p); float S = layoutScale(); return length(vec2(length(q.xz) - 0.34 * S, q.y)) - 0.075 * S; }
float hitTorus(vec3 ro, vec3 rd, out vec3 n)
{
    vec3 dummy;
    float tb = hitSphere(ro, rd, vec4(torusCentre(), 0.44 * layoutScale()), dummy);
    if (tb >= kInf) return kInf;
    float t = tb;
    for (int i = 0; i < 48; ++i) {
        float d = sdTorus(ro + rd * t);
        if (d < 1e-4) {
            vec2 e = vec2(1e-3, -1e-3);
            vec3 p = ro + rd * t;
            n = normalize(e.xyy * sdTorus(p + e.xyy) + e.yyx * sdTorus(p + e.yyx) + e.yxy * sdTorus(p + e.yxy) + e.xxx * sdTorus(p + e.xxx));
            return t;
        }
        t += d;
        if (t > tb + 0.9 * layoutScale()) break;
    }
    return kInf;
}
// the marble column, with a base and a capital
vec3 columnPos() { vec3 c = setCentre(); float S = layoutScale(); return vec3(c.x - 2.55 * S, 0.0, c.z - 1.2 * S); }
float hitCylinder(vec3 ro, vec3 rd, vec3 base, float r, float h, out vec3 n)
{
    vec2 o = ro.xz - base.xz, d = rd.xz;
    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - r * r, disc = b * b - a * c;
    float tBest = kInf;
    if (disc >= 0.0 && a > 1e-8) {
        float t = (-b - sqrt(disc)) / a;
        float y = ro.y + rd.y * t;
        if (t > 1e-3 && y > base.y && y < base.y + h) { tBest = t; n = normalize(vec3(o + d * t, 0.0).xzy); n = normalize(vec3(n.x, 0.0, n.y)); }
    }
    if (abs(rd.y) > 1e-6) {   // the top cap
        float t = (base.y + h - ro.y) / rd.y;
        vec2 p = o + d * t;
        if (t > 1e-3 && t < tBest && dot(p, p) < r * r) { tBest = t; n = vec3(0, 1, 0); }
    }
    return tBest;
}
float hitColumn(vec3 ro, vec3 rd, out vec3 n)
{
    vec3 p = columnPos();
    float S = layoutScale();
    float h = 2.2 * S;
    vec3 n1, n2, n3;
    float t = hitCylinder(ro, rd, p, 0.17 * S, h, n1);
    float tb = hitBox(ro, rd, p + vec3(-0.27, 0.0, -0.27) * S, p + vec3(0.27, 0.14, 0.27) * S, n2);
    float tc = hitBox(ro, rd, p + vec3(-0.25 * S, h, -0.25 * S), p + vec3(0.25 * S, h + 0.12 * S, 0.25 * S), n3);
    n = n1;
    if (tb < t) { t = tb; n = n2; }
    if (tc < t) { t = tc; n = n3; }
    return t;
}
// the stand under the set
float hitStand(vec3 ro, vec3 rd, out vec3 n, out int mat)
{
    vec3 c = setCentre();
    mat = 0;
    if (uStand == 2 || uDrop < 0.05) return kInf;
    if (uStand == 1) {   // marble plinth
        vec3 hs = vec3(max(0.35, (uSetMax.x - uSetMin.x) * 0.42), 0.0, max(0.3, (uSetMax.z - uSetMin.z) * 0.42));
        mat = 4;
        return hitBox(ro, rd, vec3(c.x - hs.x, 0.0, c.z - hs.z), vec3(c.x + hs.x, uDrop, c.z + hs.z), n);
    }
    // chrome pedestal: a column on a round base, with a top plate
    vec3 n1, n2, n3;
    float t = hitCylinder(ro, rd, vec3(c.x, 0.0, c.z), 0.09, uDrop, n1);
    float tb = hitCylinder(ro, rd, vec3(c.x, 0.0, c.z), 0.42, 0.05, n2);
    float tt = hitCylinder(ro, rd, vec3(c.x, uDrop - 0.04, c.z), 0.30, 0.04, n3);
    n = n1;
    if (tb < t) { t = tb; n = n2; }
    if (tt < t) { t = tt; n = n3; }
    mat = 2;
    return t;
}

// ---- the other object sets: shapes by distance function (each inside a bounding sphere) --------
mat2 rot(float a) { float c = cos(a), s = sin(a); return mat2(c, -s, s, c); }
float smin(float a, float b, float k) { float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0); return mix(b, a, h) - k * h * (1.0 - h); }
float sdCapsule(vec3 p, vec3 a, vec3 b, float r) { vec3 pa = p - a, ba = b - a; return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0)) - r; }
float sdEllipsoid(vec3 p, vec3 r) { float k0 = length(p / r), k1 = length(p / (r * r)); return k0 * (k0 - 1.0) / k1; }
float sdRoundBox(vec3 p, vec3 b, float r) { vec3 q = abs(p) - b + r; return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0) - r; }
float sdOcta(vec3 p, float s) { p = abs(p); return (p.x + p.y + p.z - s) * 0.57735027; }
float sdCone(vec3 p, float h, float r)   // tip up at y = h, base radius r at y = 0
{
    vec2 q = vec2(length(p.xz), p.y);
    vec2 k1 = vec2(0.0, h), k2 = vec2(-r, h);
    vec2 ca = vec2(q.x - min(q.x, q.y < 0.0 ? r : 0.0), abs(q.y - h * 0.5) - h * 0.5);
    vec2 cb = q - k1 + k2 * clamp(dot(k1 - q, k2) / dot(k2, k2), 0.0, 1.0);
    float sg = (cb.x < 0.0 && ca.y < 0.0) ? -1.0 : 1.0;
    return sg * sqrt(min(dot(ca, ca), dot(cb, cb)));
}
// A jointed wooden artist's mannequin (feet at the origin, facing +z), walking: the body
// rides on the hips, whose height follows the legs, so the planted foot stays on the
// floor; the leg swinging forward bends its knee and lifts its foot.
float sdMannequin(vec3 p, float ph)
{
    const float thigh = 0.44, shin = 0.44;
    float a = 0.36 * sin(ph);
    float hipY = 0.05 + (thigh + shin) * cos(a);
    float dy = hipY - 0.95;                          // the body was modelled with the hips at 0.95
    float d = 1e9;
    for (int side = -1; side <= 1; side += 2) {
        float ang = float(side) * a;                  // + forward
        float bend = max(0.0, float(side) * cos(ph)) * 0.75;   // swinging forward: knee bends
        vec3 hip = vec3(0.10 * float(side), hipY, 0.0);
        vec3 knee = hip + vec3(0.0, -thigh * cos(ang), thigh * sin(ang));
        vec3 foot = knee + vec3(0.0, -shin * cos(ang - bend), shin * sin(ang - bend));
        d = min(d, sdCapsule(p, hip, knee, 0.066));
        d = min(d, sdCapsule(p, knee, foot, 0.056));
        d = min(d, length(p - knee) - 0.072);                                   // knee joint
        d = min(d, sdEllipsoid(p - foot - vec3(0.0, -0.01, 0.07), vec3(0.055, 0.04, 0.12)));
        float arm = -ang * 0.9;                                                 // arms swing opposite
        vec3 sh = vec3(0.23 * float(side), 1.47 + dy, 0.0);
        vec3 elbow = sh + vec3(0.02 * float(side), -0.30 * cos(arm), 0.30 * sin(arm));
        vec3 hand = elbow + vec3(0.0, -0.27 * cos(arm * 0.6 + 0.25), 0.27 * sin(arm * 0.6 + 0.25));
        d = min(d, sdCapsule(p, sh, elbow, 0.048));
        d = min(d, sdCapsule(p, elbow, hand, 0.042));
        d = min(d, length(p - elbow) - 0.055);
        d = min(d, length(p - sh) - 0.065);
        d = min(d, sdEllipsoid(p - hand, vec3(0.045, 0.07, 0.035)));
    }
    vec3 b = p - vec3(0.0, dy, 0.0);
    d = min(d, sdEllipsoid(b - vec3(0.0, 1.00, 0.0), vec3(0.17, 0.11, 0.10)));   // pelvis
    d = smin(d, sdEllipsoid(b - vec3(0.0, 1.33, 0.0), vec3(0.20, 0.21, 0.12)), 0.03);   // chest
    d = min(d, sdCapsule(b, vec3(0.0, 1.10, 0.0), vec3(0.0, 1.20, 0.0), 0.08));  // waist
    d = min(d, sdCapsule(b, vec3(0.0, 1.52, 0.0), vec3(0.0, 1.62, 0.0), 0.04));  // neck
    d = min(d, sdEllipsoid(b - vec3(0.0, 1.74, 0.01), vec3(0.095, 0.125, 0.105)));   // head
    return d;
}

// Objects: the chosen set around the set (foreground), mannequins walking a loop behind it,
// and larger mixed shapes floating in the distance (the background crowd).
int fgCount() { return (uObjectSet == 0 || uObjectSet == 3) ? 0 : 6; }
int walkerCount() { return uObjectSet == 3 ? 6 : (uBackground ? 2 : 0); }
int bgCount() { return uBackground ? 8 : 0; }
int objCount() { return fgCount() + walkerCount() + bgCount(); }
// Walker j: feet position and heading (angle from +z towards +x), and its walk phase. The
// loop is a circle behind the set; the phase follows the distance walked (no foot sliding).
const float kWalkR = 5.0, kWalkSpeed = 0.42, kStride = 1.3;   // stride: distance per walk cycle
vec3 walkerFeet(int j, out float heading, out float phase)
{
    vec3 c = setCentre();
    float n = float(walkerCount());
    float R = kWalkR * layoutScale();
    float dist = kWalkSpeed * uTime + float(j) * 6.2832 * R / n;
    float th = dist / R;
    vec2 centre = vec2(c.x, c.z - 7.0 * layoutScale());
    vec2 pos = centre + R * vec2(cos(th), sin(th));
    vec2 tang = vec2(-sin(th), cos(th));
    heading = atan(tang.x, tang.y);
    phase = dist / kStride * 6.2832;
    return vec3(pos.x, 0.0, pos.y);
}
vec4 objBound(int i)
{
    vec3 c = setCentre();
    int fg = fgCount(), wk = walkerCount();
    if (i >= fg && i < fg + wk) {
        float h, ph;
        return vec4(walkerFeet(i - fg, h, ph) + vec3(0.0, 0.95, 0.0), 1.1);
    }
    if (i >= fg + wk) {   // background crowd: a ring 10-14 away, floating and bobbing
        int k = i - fg - wk;
        float a = float(k) * 0.7854 + 0.3;
        float S = layoutScale();
        float r = (10.0 + float(k % 3) * 2.0) * S;
        float y = uDrop + (1.0 + mod(float(k) * 0.7, 2.5) + sin(uTime * 0.4 + float(k)) * 0.15) * S;
        return vec4(c.x + r * sin(a), y, c.z - r * cos(a) * 0.9, (0.9 + float(k % 3) * 0.25) * S);
    }
    float b = sin(uTime * 0.6 + float(i) * 1.7) * 0.06;
    vec3 pos[6] = vec3[](vec3(c.x - 1.75, uDrop + 0.55, c.z + 0.55), vec3(c.x + 1.85, uDrop + 1.05, c.z - 0.25),
                         vec3(c.x + 0.95, uDrop + 1.95, c.z - 1.3), vec3(c.x - 2.4, uDrop + 1.6, c.z - 1.1),
                         vec3(c.x + 2.2, uDrop + 0.2, c.z + 0.9), vec3(c.x - 0.9, uDrop + 2.25, c.z - 0.9));
    float rad[6] = float[](0.60, 0.54, 0.48, 0.56, 0.46, 0.40);
    float S = layoutScale();
    vec3 off = pos[i] - vec3(c.x, uDrop, c.z);   // around the set, scaled with it
    return vec4(vec3(c.x, uDrop, c.z) + off * S + vec3(0.0, b, 0.0), rad[i] * S);
}
// kind: 0 capsule, 1 cone, 2 twisted ring, 3 checked cube, 4 octahedron, 5 sphere (toybox);
// 6 spotted blob, 7 jelly flower, 8 pearl, 9 spotted ball, 10 marbled egg (organic); 11 mannequin
int objKind(int i)
{
    int fg = fgCount(), wk = walkerCount();
    if (i >= fg && i < fg + wk) return 11;
    if (i >= fg + wk) { int bg[8] = int[](0, 7, 9, 3, 10, 1, 8, 6); return bg[(i - fg - wk) % 8]; }
    if (uObjectSet == 1) return i;
    if (uObjectSet == 2) return i == 5 ? 8 : 6 + i;
    int mixed[6] = int[](0, 7, -1, 9, 1, 8);   // slot 2: a chrome sphere (from the default set)
    return mixed[i];
}
float sdObj(int i, vec3 p)
{
    vec4 bs = objBound(i);
    int k = objKind(i);
    float t = uTime + float(i) * 1.3;
    vec3 q = p - bs.xyz;
    if (k == 11) {   // a walker, turned to face where it is going
        float h, ph;
        vec3 feet = walkerFeet(i - fgCount(), h, ph);
        vec3 w = p - feet;
        float ch = cos(h), sh = sin(h);
        vec3 l = vec3(ch * w.x - sh * w.z, w.y, sh * w.x + ch * w.z);
        return sdMannequin(l, ph);
    }
    q.xz = rot(t * 0.45) * q.xz;
    q.xy = rot(0.35 * sin(t * 0.3)) * q.xy;
    float r = bs.w;
    if (k == 0) return sdCapsule(q, vec3(0.0, -r * 0.55, 0.0), vec3(0.0, r * 0.55, 0.0), r * 0.38);
    if (k == 1) return sdCone(q + vec3(0.0, r * 0.55, 0.0), r * 1.2, r * 0.6);
    if (k == 2) { vec3 w = q; w.xy = rot(q.z * 3.0) * w.xy; return length(vec2(length(w.xz) - r * 0.7, w.y)) - r * 0.18 - 0.03 * sin(atan(w.z, w.x) * 6.0); }
    if (k == 3) return sdRoundBox(q, vec3(r * 0.52), r * 0.12);
    if (k == 4) return sdOcta(q, r * 0.85);
    if (k == 5) return length(q) - r * 0.8;
    if (k == 6) { float d = length(q - vec3(0.0, 0.05, 0.0)) - r * 0.55;
                  d = smin(d, length(q - vec3(r * 0.38, -r * 0.2, 0.1)) - r * 0.38, 0.15);
                  return smin(d, length(q - vec3(-r * 0.3, -r * 0.25, -0.12)) - r * 0.34, 0.15); }
    if (k == 7) { q.yz = rot(1.15) * q.yz;       // jelly flower, turned to face the viewer: petals round a heart
                  float d = length(q) - r * 0.24;
                  for (int j = 0; j < 6; ++j) { float a = float(j) * 1.0472; vec3 pp = q; pp.xz = rot(a) * pp.xz;
                      d = smin(d, sdEllipsoid(pp - vec3(r * 0.45, 0.0, 0.0), vec3(r * 0.42, r * 0.26, r * 0.26)), 0.08); }
                  return d; }
    if (k == 8) return length(q) - r * 0.75;
    if (k == 9) return length(q) - r * 0.9;
    return sdEllipsoid(q, vec3(r * 0.65, r * 0.9, r * 0.65));   // 10: egg
}
float hitObjects(vec3 ro, vec3 rd, out vec3 n, out int kind, out int which)
{
    float best = kInf; kind = -1; which = -1;
    for (int i = 0; i < 16; ++i) {
        if (i >= objCount()) break;
        if (objKind(i) < 0) continue;
        vec4 bs = objBound(i);
        vec3 dn;
        vec3 oc = ro - bs.xyz;
        float b = dot(oc, rd), c = dot(oc, oc) - bs.w * bs.w, h = b * b - c;
        if (h < 0.0) continue;
        float tEnter = max(-b - sqrt(h), 1e-3), tExit = -b + sqrt(h);
        if (tExit < 1e-3 || tEnter > best) continue;
        float t = tEnter;
        for (int j = 0; j < 64; ++j) {
            float d = sdObj(i, ro + rd * t);
            if (d < 4e-4 * max(t, 1.0)) {
                if (t < best) {
                    best = t; which = i; kind = objKind(i);
                    vec3 p = ro + rd * t;
                    const vec2 e = vec2(1e-3, -1e-3);
                    n = normalize(e.xyy * sdObj(i, p + e.xyy) + e.yyx * sdObj(i, p + e.yyx) + e.yxy * sdObj(i, p + e.yxy) + e.xxx * sdObj(i, p + e.xxx));
                }
                break;
            }
            t += d * 0.9;
            if (t > tExit) break;
        }
    }
    return best;
}

// materials: 1 floor, 2 chrome, 3 glass, 4 marble, 5 TV cabinet, 6 TV picture, 7 shaped object
float trace(vec3 ro, vec3 rd, bool withTv, out vec3 n, out int mat)
{
    float t = kInf; mat = 0;
    vec3 nn; int m;
    if (rd.y < -1e-5) { float tf = -ro.y / rd.y; if (tf > 1e-3) { t = tf; n = vec3(0, 1, 0); mat = 1; } }
    float ts = hitStand(ro, rd, nn, m);
    if (ts < t) { t = ts; n = nn; mat = m; }
    if (uObjects && uObjectSet == 0) {   // the default: chrome spheres, glass cube, chrome ring, marble column
        for (int i = 0; i < 3; ++i) { float th = hitSphere(ro, rd, sphere(i), nn); if (th < t) { t = th; n = nn; mat = 2; } }
        float tc = hitCube(ro, rd, nn); if (tc < t) { t = tc; n = nn; mat = 3; }
        float tt = hitTorus(ro, rd, nn); if (tt < t) { t = tt; n = nn; mat = 2; }
        float tk = hitColumn(ro, rd, nn); if (tk < t) { t = tk; n = nn; mat = 4; }
    }
    if (uObjects && uObjectSet == 4) { float th = hitSphere(ro, rd, sphere(2), nn); if (th < t) { t = th; n = nn; mat = 2; } }
    if (uObjects && objCount() > 0) {   // the chosen set's shapes, the walkers and the background crowd
        int kind, which;
        float to = hitObjects(ro, rd, nn, kind, which);
        if (to < t) { t = to; n = nn; mat = 7; gKind = kind; gWhich = which; }
    }
    if (withTv) {
        float tv = hitBox(ro, rd, uSetMin, uSetMax, nn);
        if (tv < t) {
            t = tv; n = nn; mat = 5;
            vec3 p = ro + rd * tv;
            if (nn.z > 0.5 && p.x > uScreenRect.x && p.x < uScreenRect.y && p.y > uScreenRect.z && p.y < uScreenRect.w) mat = 6;
        }
    }
    return t;
}
bool shadowed(vec3 p, vec3 l)
{
    if (!uShadows) return false;
    vec3 n; int m;
    float t = trace(p + l * 2e-3, l, true, n, m);
    return t < kInf && m != 1;
}
vec3 picture(vec3 p)
{
    if (!uHasGlass) return vec3(0.0);
    vec2 g = vec2((p.x - uScreenRect.x) / (uScreenRect.y - uScreenRect.x), (p.y - uScreenRect.z) / (uScreenRect.w - uScreenRect.z));
    return textureLod(uGlassTex, g, 1.0).rgb;
}
vec3 picAvg() { return uHasGlass ? textureLod(uGlassTex, vec2(0.5), uGlassLod).rgb : vec3(0.0); }
vec3 tvLight(vec3 p, vec3 n)   // the picture's glow on what is in front of the set
{
    vec3 c = vec3(0.5 * (uScreenRect.x + uScreenRect.y), 0.5 * (uScreenRect.z + uScreenRect.w), uScreenZ);
    vec3 L = c - p;
    float d2 = dot(L, L);
    vec3 l = L * inversesqrt(d2);
    return picAvg() * max(-l.z, 0.0) * max(dot(n, l), 0.0) * 1.2 / (d2 + 0.2);
}

vec3 floorAlbedo(vec3 p, float fw, out float emissive)
{
    emissive = 0.0;
    vec2 q = p.xz;
    if (uFloorStyle == 1) {
        // neon grid: dark glossy floor, glowing lines every unit
        vec2 g = abs(fract(q) - 0.5);
        float w = max(fw * 1.2, 0.012);
        float line = max(1.0 - smoothstep(0.5 - w, 0.5, 0.5 - g.x) , 1.0 - smoothstep(0.5 - w, 0.5, 0.5 - g.y));
        line = max(1.0 - smoothstep(w * 0.5, w, g.x - 0.0) * 0.0, line);
        float lx = 1.0 - smoothstep(0.0, w, 0.5 - g.x), lz = 1.0 - smoothstep(0.0, w, 0.5 - g.y);
        emissive = max(lx, lz) * (1.0 - smoothstep(0.3, 1.0, fw * 3.0));
        return vec3(0.015, 0.012, 0.03);
    }
    // checkerboard, box-filtered (no flicker in the distance)
    vec2 w = vec2(max(fw, 1e-4));
    vec2 i = 2.0 * (abs(fract((q - 0.5 * w) * 0.5) - 0.5) - abs(fract((q + 0.5 * w) * 0.5) - 0.5)) / w;
    float c = 0.5 - 0.5 * i.x * i.y;
    vec3 a = uPalette == 1 ? vec3(0.95, 0.78, 0.60) : uPalette == 2 ? vec3(0.32, 0.33, 0.42) : vec3(0.92, 0.92, 0.95);
    vec3 b = uPalette == 1 ? vec3(0.28, 0.07, 0.18) : uPalette == 2 ? vec3(0.02, 0.02, 0.05) : vec3(0.03, 0.03, 0.05);
    return mix(a, b, c);
}
vec3 neonColour() { return uPalette == 1 ? vec3(1.0, 0.35, 0.65) : uPalette == 2 ? vec3(0.35, 0.6, 1.0) : vec3(0.95, 0.25, 0.95); }
vec3 marble(vec3 p)
{
    float v = sin(p.x * 6.0 + p.y * 3.0 + noise2(p.xy * 3.0 + p.z) * 5.0 + noise2(p.zy * 7.0) * 2.0);
    return mix(vec3(0.92, 0.90, 0.86), vec3(0.45, 0.42, 0.44), pow(1.0 - abs(v), 6.0));
}
vec3 direct(vec3 p, vec3 n, vec3 alb, float specK, float gloss, vec3 V)
{
    vec3 L = sunDir();
    float sh = shadowed(p, L) ? 0.0 : 1.0;
    float diff = max(dot(n, L), 0.0) * sh;
    float spec = pow(max(dot(normalize(L + V), n), 0.0), gloss) * specK * sh;
    return alb * (skyAmbient() + sunColour() * diff + tvLight(p, n)) + sunColour() * spec;
}

vec3 hsv2rgb(float h, float s, float v) { vec3 k = clamp(abs(mod(h * 6.0 + vec3(0, 4, 2), 6.0) - 3.0) - 1.0, 0.0, 1.0); return v * mix(vec3(1.0), k, s); }
// Surface of a shaped object. Returns the colour lit here; for jelly, sets `through` (the
// ray carries on through it, tinted).
vec3 shadeObject(int kind, int which, vec3 p, vec3 n, vec3 V, out bool through, out vec3 tint)
{
    through = false; tint = vec3(1.0);
    vec4 bs = objBound(which);
    vec3 q = p - bs.xyz;
    if (kind <= 5) {   // toybox: glossy plastic in saturated colours, some striped or checked
        vec3 cols[6] = vec3[](vec3(0.9, 0.12, 0.10), vec3(1.0, 0.78, 0.08), vec3(0.10, 0.35, 0.95),
                              vec3(0.12, 0.75, 0.25), vec3(0.62, 0.18, 0.85), vec3(1.0, 0.35, 0.65));
        vec3 alb = cols[kind];
        if (kind == 0) alb = mix(alb, vec3(0.95), step(0.5, fract(q.y * 5.0)));                              // stripes
        if (kind == 3) alb = mix(alb, vec3(1.0, 0.9, 0.1), step(0.0, sin(q.x * 14.0) * sin(q.y * 14.0) * sin(q.z * 14.0)));   // checks
        vec3 c = direct(p, n, alb, 0.9, 140.0, V);
        c += alb * pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.35;   // rim
        return c;
    }
    if (kind == 6 || kind == 9 || kind == 10) {   // organic: bumpy, spotted or marbled skins
        vec3 bump = vec3(noise2(p.xy * 38.0) - 0.5, noise2(p.yz * 38.0 + 3.0) - 0.5, noise2(p.zx * 38.0 + 7.0) - 0.5);
        vec3 nb = normalize(n + bump * 0.35);
        float spots = smoothstep(0.55, 0.6, noise2(p.xy * 5.0 + noise2(p.zy * 4.0) * 2.0));
        vec3 alb;
        if (kind == 6) alb = mix(vec3(0.20, 0.55, 0.25), vec3(0.05, 0.22, 0.12), spots);                 // green camouflage
        else if (kind == 9) alb = mix(vec3(0.82, 0.84, 0.86), vec3(0.10, 0.25, 0.32), spots);            // pale, dark spots
        else alb = mix(vec3(0.95, 0.55, 0.20), vec3(0.35, 0.65, 0.25), smoothstep(0.35, 0.65, noise2(p.xz * 6.0 + p.y * 3.0)));   // marbled egg
        return direct(p, nb, alb, 0.35, 30.0, V);
    }
    if (kind == 7) {   // glowing translucent jelly
        float F = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
        through = true;
        tint = vec3(1.0, 0.45, 0.85) * 0.75;
        return vec3(1.0, 0.25, 0.65) * 0.65 + vec3(0.55, 0.3, 1.0) * F * 0.9 + sunColour() * pow(max(dot(reflect(-V, n), sunDir()), 0.0), 60.0);
    }
    if (kind == 8) {   // iridescent pearl
        float a = dot(n, V);
        vec3 alb = hsv2rgb(fract(0.55 + a * 0.6 + noise2(p.xy * 3.0) * 0.2), 0.45, 0.95);
        return direct(p, n, alb, 0.8, 90.0, V);
    }
    // 11: mannequin wood: pale beech, fine grain along the limbs, darker joints
    float grain = noise2(vec2(p.y * 60.0, (p.x + p.z) * 4.0));
    vec3 alb = mix(vec3(0.86, 0.66, 0.42), vec3(0.72, 0.50, 0.30), grain * 0.6);
    return direct(p, n, alb, 0.3, 40.0, V);
}

void main()
{
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    vec4 a = uInvVP * vec4(ndc, -1.0, 1.0), b = uInvVP * vec4(ndc, 1.0, 1.0);
    vec3 rw = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 O = uFloorPoint - uFloorNormal * uDrop;
    vec3 ro = vec3(dot(uCamPos - O, uFloorX), dot(uCamPos - O, uFloorNormal), dot(uCamPos - O, uFloorZ));
    vec3 rd = vec3(dot(rw, uFloorX), dot(rw, uFloorNormal), dot(rw, uFloorZ));

    vec3 col = vec3(0.0), thr = vec3(1.0);
    float depthT = kInf;
    bool first = true;
    bool floorFirst = false;   // the first surface is the floor: your models' mirror images go there
    for (int bounce = 0; bounce < 4; ++bounce) {
        vec3 n; int mat;
        float t = trace(ro, rd, !first, n, mat);   // the real set is drawn over the first hit
        if (first) { depthT = t; floorFirst = mat == 1; }
        first = false;
        if (t >= kInf) { col += thr * sky(rd); break; }
        vec3 p = ro + rd * t, V = -rd;
        float haze = 1.0 - exp(-max(t - 6.0, 0.0) * 0.03);   // distance haze into the horizon
        vec3 hz = horizonColour() * 0.9;
        if (mat == 6) { col += thr * picture(p); break; }
        if (mat == 5) { col += thr * direct(p, n, vec3(0.06, 0.06, 0.07), 0.3, 40.0, V); break; }
        if (mat == 4) { col += thr * mix(direct(p, n, marble(p), 0.35, 60.0, V), hz, haze); break; }
        if (mat == 7) {
            bool through; vec3 tint;
            vec3 c = shadeObject(gKind, gWhich, p, n, V, through, tint);
            col += thr * mix(c, hz, haze);
            if (!through) break;
            thr *= tint;
            ro = p + rd * (objBound(gWhich).w * 1.9);   // on through the jelly
            continue;
        }
        if (mat == 1) {
            float fw = length(fwidth(p.xz));
            float em;
            vec3 alb = floorAlbedo(p, fw, em);
            vec3 base = direct(p, n, alb, 0.25, 80.0, V) + neonColour() * em * 1.4;
            float refl = (uFloorStyle == 1 ? 0.35 : 0.30) * (1.0 - haze);
            col += thr * mix(base, hz, haze) * (1.0 - refl);
            thr *= refl;
            if (bounce >= uBounces) { col += thr * sky(reflect(rd, n)); break; }
            ro = p + n * 1e-3; rd = reflect(rd, n);
            continue;
        }
        if (mat == 2) {   // chrome: a tinted mirror, with a sun highlight
            col += thr * sunColour() * pow(max(dot(reflect(rd, n), sunDir()), 0.0), 400.0) * 2.0 * (shadowed(p, sunDir()) ? 0.0 : 1.0);
            thr *= vec3(0.86, 0.87, 0.9);
            if (bounce >= uBounces) { col += thr * sky(reflect(rd, n)); break; }
            ro = p + n * 1e-3; rd = reflect(rd, n);
            continue;
        }
        if (mat == 3) {   // glass: a faint reflection, then on through the cube with a tint
            float F = 0.04 + 0.96 * pow(1.0 - max(dot(n, V), 0.0), 5.0);
            col += thr * sky(reflect(rd, n)) * F;
            thr *= vec3(0.78, 0.92, 0.95) * (1.0 - F);
            // step through: out of the far side of the cube
            vec3 nn;
            float a2 = cubeAngle(), ca = cos(a2), sa = sin(a2);
            vec3 o = p + rd * 0.7;
            ro = o; // continue from beyond the cube (it is at most ~0.52 across its diagonal)
            continue;
        }
        break;
    }
    if (uBanding) col = floor(clamp(col, 0.0, 1.0) * 20.0 + 0.5) / 20.0;   // 90s colour depth
    fragColor = vec4(clamp(col, 0.0, 1.0), floorFirst ? 0.0 : 1.0);   // (alpha is reset to 1 after the mirror images)
    // depth of the first surface, so the real set and the traced objects hide each other
    if (depthT >= kInf) gl_FragDepth = 0.999999;
    else {
        vec3 W = uCamPos + rw * depthT;
        vec4 clip = uVP * vec4(W, 1.0);
        gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 0.999999);
    }
}
