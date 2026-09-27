#version 330 core
// Desk scene (desk mode): a wooden desk on the set's floor plane, a plastered wall behind
// the set, a lamp (by mood), light from the TV's own picture, the picture's reflection in
// the desk's clear-coat, and optional drifting fog lit by the screen.
out vec4 fragColor;
uniform mat4 uInvVP;
uniform vec2 uViewport;
uniform vec3 uCamPos;
// floor (the plane the set's shadow lies on) and its axes
uniform vec3 uFloorPoint;
uniform vec3 uFloorNormal;
uniform vec3 uFloorX;
uniform vec3 uFloorZ;
uniform vec2 uSetCentre;     // footprint centre, floor coordinates
uniform vec2 uSetHalf;       // footprint half size
// the screen as a light source, and its picture
uniform vec3 uScreenCentre;
uniform vec3 uScreenRight;   // unit vectors in world space
uniform vec3 uScreenUp;
uniform vec3 uScreenNormal;
uniform vec2 uScreenHalf;    // half width / height, world units
uniform mat4 uInvModel;      // world -> set model space (for the picture's uv)
uniform float uAspect;
uniform bool uPivot;
uniform sampler2D uGlassTex; // CRT output on the glass (bottom-up, mipmapped)
uniform bool uHasGlass;
uniform float uGlassLod;     // mip level that averages the whole picture
// scene options
uniform int uScene;          // 1 desk, 2 wall-mounted TV
uniform float uWallQ;        // the wall's position (floor coordinates, along uFloorZ)
uniform float uFloorDrop;    // the floor lies this far below uFloorPoint (wall scene)
uniform int uWallStyle;      // 0 warm white, 1 sage, 2 navy, 3 charcoal, 4 pinstripe, 5 damask
uniform vec4 uSetRect;       // the set on the wall: u0, u1, v0, v1 (v = height above the floor)
uniform float uSetDepth;     // how far the set stands off the wall
uniform float uRoomHalfW;    // side walls: this far either side of the set
uniform float uCeil;         // the ceiling's height above the floor
// picture frames (wall scene): centre u, centre v, half width, half height (incl. the mat)
uniform int uFrameCount;
uniform vec4 uFrame[4];
uniform int uFrameStyle;     // 0 black, 1 wood, 2 gold
uniform sampler2D uFrameTex0;
uniform sampler2D uFrameTex1;
uniform sampler2D uFrameTex2;
uniform sampler2D uFrameTex3;
uniform int uMood;           // 0 evening, 1 night, 2 lights off
uniform int uWood;           // 0 walnut, 1 oak, 2 cherry
uniform float uFog;          // 0 = off
uniform int uFogSteps;
uniform float uTime;

float hash1(float n) { return fract(sin(n) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise1(float x) { float i = floor(x), f = fract(x); return mix(hash1(i), hash1(i + 1.0), f * f * (3.0 - 2.0 * f)); }
float noise2(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}

// ---- the picture as light ------------------------------------------------------------
vec3 pictureAt(vec2 g, float lod)   // g: glass uv, top-down
{
    return textureLod(uGlassTex, vec2(g.x, 1.0 - g.y), lod).rgb;
}
vec3 screenAverage(float side)       // side: -1 left half, 0 whole, +1 right half
{
    if (!uHasGlass) return vec3(0.0);
    if (side == 0.0) return pictureAt(vec2(0.5), uGlassLod);
    return pictureAt(vec2(0.5 + side * 0.25, 0.5), max(uGlassLod - 1.5, 0.0));
}
// Light arriving at P (normal N) from the screen: two sub-lights (left and right halves).
vec3 screenIrradiance(vec3 P, vec3 N)
{
    vec3 sum = vec3(0.0);
    for (int i = -1; i <= 1; i += 2) {
        vec3 c = uScreenCentre + uScreenRight * (float(i) * uScreenHalf.x * 0.5);
        vec3 L = c - P;
        float d2 = dot(L, L);
        vec3 l = L * inversesqrt(d2);
        float emit = max(dot(-l, uScreenNormal), 0.0);     // leaves the front of the screen
        float recv = max(dot(l, N), 0.0);
        float area = uScreenHalf.x * uScreenHalf.y * 2.0;   // half the screen each
        sum += screenAverage(float(i)) * emit * recv * area / (d2 + 0.08);
    }
    return sum * 4.5;   // a TV visibly lights the desk in front of it in a dark room
}

// ---- the lamp and the mood --------------------------------------------------------------
vec3 lampPos() { return uFloorPoint + uFloorX * (uSetCentre.x - 1.4) + uFloorNormal * 1.3 + uFloorZ * (uSetCentre.y + 0.4); }
vec3 lampColour() { return uMood == 0 ? vec3(1.0, 0.78, 0.52) * 1.1 : uMood == 1 ? vec3(1.0, 0.75, 0.5) * 0.28 : vec3(0.0); }
vec3 ambient() { return uMood == 0 ? vec3(0.10, 0.085, 0.07) : uMood == 1 ? vec3(0.035, 0.042, 0.06) : vec3(0.012, 0.012, 0.016); }
vec3 lampLight(vec3 P, vec3 N)
{
    vec3 L = lampPos() - P;
    float d2 = dot(L, L);
    return lampColour() * max(dot(normalize(L), N), 0.0) * 1.6 / (d2 + 0.6);
}

// ---- the desk: planks of veneered wood ---------------------------------------------------------------
vec3 deskWood(vec2 q, float footprint, out float seam)
{
    const float plankW = 0.16;                       // ~15 cm boards (the set is ~1 unit = ~50 cm tall)
    float row = floor(q.y / plankW);
    float fy = fract(q.y / plankW);
    // boards are laid in lengths, staggered from row to row (end joints)
    float len = 1.3 + 0.9 * hash1(row * 7.1);
    float xo = q.x / len + hash1(row * 3.3);
    float piece = floor(xo);
    float id = row * 17.0 + piece;
    float tone = hash1(id * 1.7);
    // fine grain across the board, gently wandering along it
    float wander = (noise1(q.x * 1.3 + id) - 0.5) * 0.02 + (noise1(q.x * 5.0 + id * 2.0) - 0.5) * 0.004;
    float u = (q.y + wander) * 180.0 + noise1(q.y * 40.0 + id) * 1.5;
    float lineFade = 1.0 - smoothstep(0.25, 0.8, footprint * 180.0);
    float line = 1.0 - smoothstep(0.0, 0.14, abs(fract(u) - 0.5) - 0.36);
    line *= (0.35 + 0.65 * noise1(floor(u) * 3.1 + id)) * lineFade;
    float figure = smoothstep(0.2, 0.9, noise1((q.y + wander * 4.0) * 22.0 + noise1(q.x * 1.1 + id) * 2.0 + id));
    float pores = noise2(vec2(q.x * 260.0, floor(u))) * lineFade;
    vec3 dark, light;
    if (uWood == 1)      { dark = vec3(0.43, 0.30, 0.17); light = vec3(0.64, 0.48, 0.30); }   // oak
    else if (uWood == 2) { dark = vec3(0.30, 0.11, 0.06); light = vec3(0.50, 0.23, 0.12); }   // cherry
    else                 { dark = vec3(0.19, 0.10, 0.05); light = vec3(0.34, 0.20, 0.10); }   // walnut
    vec3 c = mix(dark, light, 0.25 + 0.5 * figure + 0.25 * tone);
    c *= 1.0 - 0.20 * line - 0.06 * pores;
    // seams: along the boards, and at the end joints
    float sw = max(footprint / plankW * 1.5, 0.004);
    float sideSeam = 1.0 - smoothstep(0.0, sw, min(fy, 1.0 - fy));
    float endSeam = 1.0 - smoothstep(0.0, max(footprint / len * 1.5, 0.0015), min(fract(xo), 1.0 - fract(xo)));
    seam = max(sideSeam, endSeam);
    return c * (1.0 - 0.55 * seam);
}

// the picture's reflection in the desk's clear-coat (blurs with distance)
vec3 pictureReflection(vec3 P, vec3 R, out float cover)
{
    cover = 0.0;
    if (!uHasGlass) return vec3(0.0);
    float den = dot(R, uScreenNormal);
    if (den > -1e-3) return vec3(0.0);               // must travel towards the screen's front
    float t = dot(uScreenCentre - P, uScreenNormal) / den;
    if (t <= 0.0) return vec3(0.0);
    vec3 h = P + R * t;
    vec4 m4 = uInvModel * vec4(h, 1.0);
    vec2 m = m4.xy / m4.w;
    float blur = 0.03 + 0.12 * t;                     // rougher with distance
    vec2 edge = abs(m) - vec2(uAspect * 0.5, 0.5);
    cover = 1.0 - smoothstep(-blur, blur, max(edge.x, edge.y));
    vec2 c = clamp(m, -vec2(uAspect * 0.5, 0.5), vec2(uAspect * 0.5, 0.5));
    vec2 g = uPivot ? vec2(0.5 - c.y, 0.5 - c.x / uAspect) : vec2(c.x / uAspect + 0.5, 0.5 - c.y);
    return pictureAt(g, clamp(3.0 + t * 6.0, 0.0, uGlassLod));
}

// ---- the wall: painted plaster or wallpaper, with a skirting board ---------------------------------
vec3 wall(vec2 w, float footprint)   // w.x along the wall, w.y height above the floor
{
    vec3 paint = uMood == 2 ? vec3(0.30, 0.29, 0.28) : vec3(0.40, 0.37, 0.33);
    if (uScene == 2) {
        if (uWallStyle == 1) paint = vec3(0.36, 0.43, 0.34);
        else if (uWallStyle == 2) paint = vec3(0.12, 0.16, 0.26);
        else if (uWallStyle == 3) paint = vec3(0.15, 0.15, 0.16);
        else paint = vec3(0.60, 0.57, 0.52);
    }
    float plaster = noise2(w * 18.0) * 0.6 + noise2(w * 70.0) * 0.4;
    vec3 c = paint * (0.94 + 0.08 * plaster);
    if (uScene == 2 && uWallStyle == 4) {   // pinstripe wallpaper
        float st = abs(fract(w.x * 9.0) - 0.5);
        float fw = max(footprint * 9.0, 0.02);
        c = mix(vec3(0.62, 0.58, 0.50), vec3(0.42, 0.26, 0.22), 1.0 - smoothstep(0.03, 0.03 + fw, st));
    } else if (uScene == 2 && uWallStyle == 5) {   // damask: a repeating two-tone ornament
        vec2 cell = fract(w * vec2(3.2, 2.4)) - 0.5;
        float r = length(cell * vec2(1.0, 1.35));
        float petal = abs(sin(atan(cell.y, cell.x) * 4.0)) * 0.16 + 0.12;
        float motif = 1.0 - smoothstep(petal - 0.02, petal + 0.02, r);
        float fade = 1.0 - smoothstep(0.3, 1.0, footprint * 3.2 * 8.0);
        c = mix(vec3(0.28, 0.08, 0.10), vec3(0.40, 0.14, 0.15), motif * fade);
    }
    if (w.y < 0.10) {                                 // skirting board, with a top edge
        c = vec3(0.62, 0.60, 0.56);
        float lip = 1.0 - smoothstep(0.0, max(footprint * 1.5, 0.004), abs(w.y - 0.095));
        c *= 1.0 - 0.35 * lip;
    }
    return c;
}

float fogDensity(vec3 p)
{
    float h = dot(p - uFloorPoint, uFloorNormal);
    vec3 drift = vec3(uTime * 0.03, 0.0, uTime * 0.02);
    float n = noise2((p.xz + drift.xz) * 0.9) * 0.6 + noise2((p.xy + drift.xy) * 1.7) * 0.4;
    return uFog * (0.55 + 0.45 * n) * exp(-max(h, 0.0) * 0.9);
}

// ---- picture frames ------------------------------------------------------------------------------
vec3 frameImage(int i, vec2 uv)
{
    if (i == 0) return texture(uFrameTex0, uv).rgb;
    if (i == 1) return texture(uFrameTex1, uv).rgb;
    if (i == 2) return texture(uFrameTex2, uv).rgb;
    return texture(uFrameTex3, uv).rgb;
}
const float kMould = 0.045;   // moulding width
const float kDepth = 0.035;   // how far a frame stands off the wall
const float kMat = 0.05;      // mat board around the image

// Ray (wall-local: u, v, w) against frame i's box; returns t (or 1e9) and the face normal.
float hitFrame(int i, vec3 ro, vec3 rd, out vec3 n)
{
    vec4 f = uFrame[i];
    vec3 bmin = vec3(f.x - f.z - kMould, f.y - f.w - kMould, 0.0);
    vec3 bmax = vec3(f.x + f.z + kMould, f.y + f.w + kMould, kDepth);
    vec3 inv = 1.0 / rd;
    vec3 t0 = (bmin - ro) * inv, t1 = (bmax - ro) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tn = max(max(tmin.x, tmin.y), tmin.z), tf = min(min(tmax.x, tmax.y), tmax.z);
    if (tn > tf || tf < 0.0 || tn < 0.0) return 1e9;
    n = tn == tmin.x ? vec3(-sign(rd.x), 0, 0) : tn == tmin.y ? vec3(0, -sign(rd.y), 0) : vec3(0, 0, -sign(rd.z));
    return tn;
}

// Brass picture lights above the frames (on in the evening and night moods).
float pictureLightLevel() { return uMood == 0 ? 1.0 : uMood == 1 ? 0.7 : 0.0; }
// wall-local position of frame i's light (just under its fixture, out from the wall)
vec3 pictureLightPos(int i) { vec4 f = uFrame[i]; return vec3(f.x, f.y + f.w + kMould + 0.05, 0.14); }
vec3 pictureLights(vec3 pl, vec3 nl)   // pl, nl: wall-local point and normal
{
    float k = pictureLightLevel();
    if (k <= 0.0) return vec3(0.0);
    vec3 sum = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        if (i >= uFrameCount) break;
        vec3 L = pictureLightPos(i) - pl;
        float d2 = dot(L, L);
        vec3 l = L * inversesqrt(d2);
        float cone = smoothstep(0.1, 0.6, l.y);               // the light is above: it shines down, onto the picture
        sum += max(dot(nl, l), 0.0) * cone / (d2 + 0.04);
    }
    return vec3(1.0, 0.80, 0.56) * 0.22 * k * sum;
}
// fixture i's box: a slim brass bar on a short arm
float hitFixture(int i, vec3 ro, vec3 rd, out vec3 n)
{
    vec4 f = uFrame[i];
    float top = f.y + f.w + kMould;
    vec3 bmin = vec3(f.x - f.z * 0.45, top + 0.035, 0.0), bmax = vec3(f.x + f.z * 0.45, top + 0.065, 0.13);
    vec3 inv = 1.0 / rd;
    vec3 t0 = (bmin - ro) * inv, t1 = (bmax - ro) * inv;
    vec3 tmin = min(t0, t1), tmax = max(t0, t1);
    float tn = max(max(tmin.x, tmin.y), tmin.z), tf = min(min(tmax.x, tmax.y), tmax.z);
    if (tn > tf || tf < 0.0 || tn < 0.0) return 1e9;
    n = tn == tmin.x ? vec3(-sign(rd.x), 0, 0) : tn == tmin.y ? vec3(0, -sign(rd.y), 0) : vec3(0, 0, -sign(rd.z));
    return tn;
}

vec3 frameMoulding(float across)   // across: 0 at the inner edge .. 1 at the outer edge
{
    vec3 c = uFrameStyle == 2 ? vec3(0.74, 0.56, 0.22) : uFrameStyle == 1 ? vec3(0.30, 0.17, 0.08) : vec3(0.035, 0.035, 0.04);
    float bead = 0.75 + 0.25 * cos(across * 6.2832 * (uFrameStyle == 2 ? 2.0 : 1.0));   // bevelled profile
    return c * bead;
}

// Soft darkening where two surfaces meet (a room's corners and edges).
float cornerAO(float d) { return 0.55 + 0.45 * smoothstep(0.0, 0.55, d); }

vec3 ceilingColour(vec2 cz, float edge, float footprint)   // cz: position on the ceiling; edge: distance to the nearest wall
{
    vec3 plaster = (uMood == 2 ? vec3(0.34, 0.33, 0.32) : vec3(0.58, 0.56, 0.53)) * (0.95 + 0.06 * noise2(cz * 14.0));
    // cornice: a moulded band where the ceiling meets the walls
    float band = 1.0 - smoothstep(0.14, 0.16, edge);
    float groove = 1.0 - smoothstep(0.0, max(footprint * 1.5, 0.006), abs(edge - 0.07));
    vec3 c = mix(plaster, plaster * 1.06, band);
    return c * (1.0 - 0.35 * groove * band);
}

void main()
{
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    vec4 a = uInvVP * vec4(ndc, -1.0, 1.0), b = uInvVP * vec4(ndc, 1.0, 1.0);
    vec3 ro = uCamPos, rd = normalize(b.xyz / b.w - a.xyz / a.w);

    // Floor and wall hits (the wall stands behind the set, facing it), and picture frames.
    float wallQ = uWallQ;
    vec3 floorPt = uFloorPoint - uFloorNormal * uFloorDrop;
    vec3 wallPoint = floorPt + uFloorZ * wallQ;
    float dF = dot(rd, uFloorNormal), dW = dot(rd, uFloorZ);
    float tF = dF < -1e-4 ? dot(floorPt - ro, uFloorNormal) / dF : 1e9;
    float tW = dW < -1e-4 ? dot(wallPoint - ro, uFloorZ) / dW : 1e9;
    if (tF < 1e9) {   // a floor point behind the wall is hidden by it
        vec3 hp = ro + rd * tF;
        if (dot(hp - uFloorPoint, uFloorZ) < wallQ) tF = 1e9;
    }
    // wall-local ray: u along the wall, v height above the floor, w out of the wall
    vec3 rol = vec3(dot(ro - wallPoint, uFloorX), dot(ro - wallPoint, uFloorNormal), dot(ro - wallPoint, uFloorZ));
    vec3 rdl = vec3(dot(rd, uFloorX), dot(rd, uFloorNormal), dot(rd, uFloorZ));
    float tFr = 1e9; int fi = -1; vec3 fn = vec3(0.0);
    float tFx = 1e9; vec3 xn = vec3(0.0);
    for (int i = 0; i < 4; ++i) {
        if (i >= uFrameCount) break;
        vec3 n; float th = hitFrame(i, rol, rdl, n);
        if (th < tFr) { tFr = th; fi = i; fn = n; }
        {   // the fixture is there even when switched off
            float tx = hitFixture(i, rol, rdl, n);
            if (tx < tFx) { tFx = tx; xn = n; }
        }
    }
    // The rest of the room: side walls either side of the set, and the ceiling. Seen from
    // inside, the nearest surface is the one hit.
    float xL = uSetCentre.x - uRoomHalfW, xR = uSetCentre.x + uRoomHalfW;
    float tSL = rdl.x < -1e-4 ? (xL - rol.x) / rdl.x : 1e9;
    float tSR = rdl.x > 1e-4 ? (xR - rol.x) / rdl.x : 1e9;
    float tC = rdl.y > 1e-4 ? (uCeil - rol.y) / rdl.y : 1e9;
    float tSide = min(tSL, tSR);
    float tRoom = min(tSide, tC);
    if (tRoom < tF) tF = 1e9;   // the floor beyond a side wall is hidden by it
    if (tRoom < tW) tW = 1e9;
    float t = min(min(tF, tW), min(min(tFr, tFx), tRoom));
    vec3 col;
    if (tFx <= t && tFx < 1e9) {
        // a brass picture light; its underside glows when it is on
        vec3 P = ro + rd * tFx;
        vec3 N = xn.x * uFloorX + xn.y * uFloorNormal + xn.z * uFloorZ;
        vec3 brass = vec3(0.70, 0.52, 0.24);
        vec3 V = -rd, L = normalize(lampPos() - P);
        col = brass * (ambient() + lampLight(P, N)) + brass * pow(max(dot(normalize(L + V), N), 0.0), 25.0) * lampColour() * 0.8;
        if (xn.y < -0.5) col += vec3(1.0, 0.85, 0.6) * 1.4 * pictureLightLevel();
    } else if (fi >= 0 && tFr <= t) {
        // A picture frame: moulding, mat, the image under glass.
        vec3 pl = rol + rdl * tFr;
        vec4 f = uFrame[fi];
        vec2 d = pl.xy - f.xy;
        vec3 P = ro + rd * tFr;
        vec3 N = fn.x * uFloorX + fn.y * uFloorNormal + fn.z * uFloorZ;
        vec3 light = ambient() + lampLight(P, N) + screenAverage(0.0) * 0.10 / (1.0 + dot(P - uScreenCentre, P - uScreenCentre))
                   + pictureLights(pl, fn);
        bool front = fn.z > 0.5;
        if (front && abs(d.x) < f.z && abs(d.y) < f.w) {
            vec2 img = vec2(f.z, f.w) - kMat;
            vec3 alb;
            if (abs(d.x) < img.x && abs(d.y) < img.y) {
                alb = frameImage(fi, vec2(d.x / img.x * 0.5 + 0.5, 0.5 - d.y / img.y * 0.5));
            } else {
                float inner = min(abs(abs(d.x) - img.x), abs(abs(d.y) - img.y));
                alb = vec3(0.86, 0.84, 0.80) * (0.85 + 0.15 * smoothstep(0.0, 0.012, inner));   // mat, bevelled cut
            }
            float edge = min(f.z - abs(d.x), f.w - abs(d.y));
            alb *= 0.75 + 0.25 * smoothstep(0.0, 0.03, edge);   // the moulding shades the mat's edge
            col = alb * light;
            // glass: a faint sheen of the room and the screen
            float F = 0.04 + 0.96 * pow(1.0 - max(dot(N, -rd), 0.0), 5.0);
            col += (screenAverage(0.0) * 0.25 + lampColour() * 0.1) * F;
        } else {
            float across = front ? clamp(max(abs(d.x) - f.z, abs(d.y) - f.w) / kMould, 0.0, 1.0) : 1.0;
            vec3 alb = frameMoulding(across) * (front ? 1.0 : 0.7);
            vec3 V = -rd, L = normalize(lampPos() - P);
            float spec = pow(max(dot(normalize(L + V), N), 0.0), uFrameStyle == 2 ? 30.0 : 50.0) * (uFrameStyle == 2 ? 0.9 : 0.25);
            col = alb * light + (uFrameStyle == 2 ? alb : vec3(1.0)) * spec * lampColour();
        }
    } else if (tRoom <= t && tRoom < 1e9) {
        vec3 P = ro + rd * tRoom;
        vec3 pl = rol + rdl * tRoom;   // wall-local: u across, v up from the floor, w out of the back wall
        float footprint = length(fwidth(pl));
        if (tC <= tSide) {
            // the ceiling
            vec3 N = -uFloorNormal;
            float edge = min(min(pl.x - xL, xR - pl.x), pl.z);
            vec3 bounce = screenAverage(0.0) * 0.05 / (1.0 + dot(P - uScreenCentre, P - uScreenCentre) * 0.15);
            col = ceilingColour(pl.xz, edge, footprint) * (ambient() + lampLight(P, N) + screenIrradiance(P, N) + bounce);
            col *= cornerAO(edge);
        } else {
            // a side wall: the same finish as the back wall, with its skirting board
            vec3 N = tSL < tSR ? uFloorX : -uFloorX;
            vec2 w = vec2(pl.z, pl.y);
            vec3 bounce = screenAverage(0.0) * 0.06 / (1.0 + dot(P - uScreenCentre, P - uScreenCentre) * 0.3);
            col = wall(w, footprint) * (ambient() + lampLight(P, N) + screenIrradiance(P, N) + bounce);
            col *= cornerAO(pl.z) * cornerAO(uCeil - pl.y) * cornerAO(pl.y * 2.0);
        }
    } else if (t >= 1e9) {
        col = ambient() * 0.4;                        // nothing there (looking away)
    } else if (tF < tW) {
        vec3 P = ro + rd * tF, N = uFloorNormal;
        vec3 d = P - floorPt;
        vec2 q = vec2(dot(d, uFloorX), dot(d, uFloorZ));
        float footprint = length(fwidth(q));
        float seam;
        vec3 wood = deskWood(q, footprint, seam);
        vec3 light = ambient() + lampLight(P, N) + screenIrradiance(P, N);
        col = wood * light;
        // satin clear-coat: Fresnel reflection of the picture, and of the lamp
        vec3 V = -rd, R = reflect(rd, N);
        float F = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        float cover;
        vec3 refl = pictureReflection(P, R, cover);
        col += refl * cover * (F * 0.9 + 0.025) * (1.0 - seam);
        vec3 Lp = normalize(lampPos() - P);
        col += lampColour() * pow(max(dot(normalize(Lp + V), N), 0.0), 60.0) * 0.12;
        col *= cornerAO(q.x - xL) * cornerAO(xR - q.x) * cornerAO((q.y - wallQ) * 2.0);   // into the room's edges
    } else {
        vec3 P = ro + rd * tW, N = uFloorZ;
        vec3 d = P - floorPt;
        vec2 w = vec2(dot(d, uFloorX), dot(d, uFloorNormal));
        float footprint = length(fwidth(w));
        // the wall behind the set only gets the screen's light bounced around the room
        vec3 bounce = screenAverage(0.0) * 0.07 / (1.0 + dot(P - uScreenCentre, P - uScreenCentre) * 0.4);
        float occl = 1.0;
        if (uScene == 2) {
            // the mounted set's soft shadow on the wall, and its light spilling around it
            vec2 c = 0.5 * (uSetRect.xz + uSetRect.yw), h = 0.5 * (uSetRect.yw - uSetRect.xz);
            vec2 q = abs(w - c) - h;
            float sd = length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
            float soft = 0.06 + uSetDepth * 0.12;
            occl *= 1.0 - 0.6 * (1.0 - smoothstep(-soft * 0.3, soft, sd));
            bounce += screenAverage(0.0) * 0.16 * exp(-max(sd, 0.0) * 3.0);
            for (int i = 0; i < 4; ++i) {   // the frames' drop shadows
                if (i >= uFrameCount) break;
                vec4 f = uFrame[i];
                vec2 fq = abs(w - f.xy - vec2(0.012, -0.02)) - (f.zw + kMould);
                float fs = length(max(fq, 0.0)) + min(max(fq.x, fq.y), 0.0);
                occl *= 1.0 - 0.35 * (1.0 - smoothstep(-0.01, 0.035, fs));
            }
        }
        vec3 pics = uScene == 2 ? pictureLights(vec3(w, 0.0), vec3(0, 0, 1)) : vec3(0.0);
        col = wall(w, footprint) * ((ambient() + lampLight(P, N) + pics) * occl + bounce);
        col *= cornerAO(w.x - xL) * cornerAO(xR - w.x) * cornerAO(uCeil - w.y);
    }

    // Fog: drifting haze, denser near the floor, scattering the screen's light towards the camera.
    if (uFog > 0.0) {
        float T = 1.0;
        vec3 inscatter = vec3(0.0);
        float tEnd = min(t, 14.0);
        float dt = tEnd / float(uFogSteps);
        vec3 fogAmb = ambient() * 0.7 + lampColour() * 0.02;
        for (int i = 0; i < 64; ++i) {
            if (i >= uFogSteps) break;
            float s = (float(i) + hash2(gl_FragCoord.xy + float(i))) * dt;   // jittered: no banding
            vec3 p = ro + rd * s;
            float dens = fogDensity(p) * dt;
            vec3 L = uScreenCentre - p;
            float d2 = dot(L, L);
            vec3 l = L * inversesqrt(d2);
            float emit = max(dot(-l, uScreenNormal), 0.0);
            float phase = 0.5 + 0.8 * pow(max(dot(-l, -rd), 0.0), 4.0);     // forward scattering
            vec3 lit = screenAverage(0.0) * emit * phase * 3.5 / (d2 + 0.25) + fogAmb;   // the screen's light dominates
            inscatter += T * dens * lit;
            T *= exp(-dens * 1.4);
        }
        col = col * T + inscatter;
    }
    fragColor = vec4(col, 1.0);
}
