#version 330 core
// Movie theater, in front of the screen (drawn after it, premultiplied alpha): the
// proscenium with its gold trim, black masking that closes in to the picture, the valance,
// the red velvet curtains, rows of seats with aisle step lights, and the projector beam.
// Theater space as in theater.frag (screen: x in [-A/2, A/2], y in [0, 1], z = 0).
out vec4 fragColor;
uniform mat4 uInvVP;
uniform vec2 uViewport;
uniform vec3 uCamPos;
uniform vec3 uFloorPoint;
uniform vec3 uFloorX;
uniform vec3 uFloorNormal;
uniform vec3 uFloorZ;
uniform float uAspect;
uniform sampler2D uGlassTex;
uniform bool uHasGlass;
uniform float uGlassLod;
uniform float uHouse;       // house lights 0..1
uniform float uCurtain;     // 0 closed .. 1 open
uniform vec4 uMask;         // masking opening: x0, x1, y0, y1 (animated towards the picture)
uniform float uBeam;        // projector beam strength (0 = off)
uniform float uFog;
uniform float uTime;
uniform bool uMarchRef;     // tests: tiny careful steps (a slow, near-exact reference)

float hash1(float n) { return fract(sin(n) * 43758.5453); }
float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float hash3(vec3 p) { return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453); }
vec3 picAvg() { return uHasGlass ? textureLod(uGlassTex, vec2(0.5), uGlassLod).rgb : vec3(0.0); }
vec3 picAt(vec2 xy, float lod)   // theater-space point on the screen -> the picture
{
    vec2 g = vec2(xy.x / uAspect + 0.5, 1.0 - xy.y);
    return uHasGlass ? textureLod(uGlassTex, vec2(g.x, 1.0 - g.y), lod).rgb : vec3(0.0);
}
float openW() { return max(uAspect, 2.39) + 0.30; }
const float kOpenY0 = -0.12, kOpenY1 = 1.12, kStageY = -0.30, kCeil = 1.9;
float wallX() { return openW() * 0.5 + 0.9; }
vec3 house() { return vec3(1.0, 0.78, 0.55) * uHouse; }

// The auditorium (shared with DeskView): stadium rows, each one step higher than the one in front.
const float kAudY = -0.45, kRow0Z = 1.2, kRowD = 0.22, kRise = 0.07, kAisleX = 0.95;
const int kRows = 20;
float rowFloor(int k) { return kAudY + kRise * float(k + 1); }
float sdBox(vec3 p, vec3 b) { vec3 q = abs(p) - b; return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0); }
float sdRBox(vec3 p, vec3 b, float r) { return sdBox(p, b - r) - r; }
// One seat centred at xc in row k: a reclined backrest, a cushion, armrests on posts.
float oneSeat(vec3 p, float xc, float zc, float fy, out int part)
{
    vec3 l = vec3(p.x - xc, p.y - fy, p.z - (zc + 0.05));                      // +z = towards the back
    float back = sdRBox(vec3(l.x, l.y - 0.135, l.z - 0.028 - (l.y - 0.135) * 0.18), vec3(0.047, 0.058, 0.011), 0.011);
    float cush = sdRBox(vec3(l.x, l.y - 0.075, l.z + 0.014), vec3(0.045, 0.011, 0.036), 0.009);
    float arm = sdRBox(vec3(abs(l.x) - 0.055, l.y - 0.095, l.z + 0.008), vec3(0.006, 0.006, 0.042), 0.004);
    float post = sdBox(vec3(abs(l.x) - 0.055, l.y - 0.045, l.z + 0.008), vec3(0.004, 0.045, 0.010));
    float d = back; part = 0;
    if (cush < d) { d = cush; part = 1; }
    float a = min(arm, post);
    if (a < d) { d = a; part = 2; }
    return d;
}
// Row k's seats near p: the nearest one and its neighbours on both sides. (Only the nearest
// would let a ray crossing the row at an angle overshoot into the next seat: jagged edges.)
float seatSdf(vec3 p, int k, vec3 cam, out int part)
{
    part = -1;
    float zc = kRow0Z + float(k) * kRowD;
    if (zc + 0.05 > cam.z - 0.10) return 1e3;                  // your own row, and those behind you
    float stag = (k % 2 == 1) ? 0.055 : 0.0;                   // staggered, like real cinema seats
    float ci = floor((p.x + stag) / 0.11 + 0.5);
    float d = 1e3;
    for (int j = -1; j <= 1; ++j) {
        float xc = (ci + float(j)) * 0.11 - stag;
        if (abs(abs(xc) - kAisleX) < 0.10 || abs(xc) > wallX() - 0.14) continue;   // aisles, walls: no seat
        int pt;
        float ds = oneSeat(p, xc, zc, rowFloor(k), pt);
        if (ds < d) { d = ds; part = pt; }
    }
    return d;
}
float stepSdf(vec3 p, int k)   // the step block under row k
{
    float zc = kRow0Z + float(k) * kRowD;
    return sdBox(vec3(p.x, p.y - (rowFloor(k) - 1.0), p.z - zc), vec3(wallX(), 1.0, kRowD * 0.5));
}
vec3 gCam;   // the camera (theater space), for the seat test
float audSdf(vec3 p, out int part, out int row)
{
    int k = int(clamp(floor((p.z - kRow0Z) / kRowD + 0.5), 0.0, float(kRows - 1)));
    float d = 1e3; part = -1; row = k;
    for (int j = -1; j <= 1; ++j) {
        int kk = k + j;
        if (kk < 0 || kk >= kRows) continue;
        int pt;
        float ds = seatSdf(p, kk, gCam, pt);
        if (ds < d) { d = ds; part = pt; row = kk; }
        float st = stepSdf(p, kk);
        if (st < d) { d = st; part = 3; row = kk; }
    }
    return d;
}

// The screen's light wrapping round the seats (they face away from it) plus a little bounce.
vec3 seatLight(vec3 p, vec3 n)
{
    vec3 L = vec3(0.0, 0.5, 0.0) - p;
    float d2 = dot(L, L);
    vec3 l = L * inversesqrt(d2);
    float wrap = pow(dot(l, n) * 0.5 + 0.5, 2.0);
    return picAvg() * (max(-l.z, 0.0) * wrap * uAspect * 4.5 / (d2 + 0.3) + 0.28);
}

vec3 screenLight(vec3 p, vec3 n)
{
    vec3 L = vec3(0.0, 0.5, 0.0) - p;
    float d2 = dot(L, L);
    vec3 l = L * inversesqrt(d2);
    return picAvg() * max(-l.z, 0.0) * max(dot(l, n), 0.0) * uAspect * 1.3 / (d2 + 0.3);
}

// red velvet: vertical folds (a fixed number, so they bunch up as the curtain opens)
vec3 velvet(float u, float width, vec3 p, vec3 V)
{
    float folds = 16.0;
    float ph = u * folds * 6.2832;
    vec3 n = normalize(vec3(sin(ph) * 0.8 * clamp(0.9 / max(width, 0.2), 0.6, 2.5), 0.0, 1.0));
    vec3 light = house() * 0.55 + screenLight(p, n) * 1.6 + vec3(0.01);
    float sheen = pow(1.0 - abs(dot(n, V)), 3.0);   // velvet: bright at grazing angles
    vec3 base = vec3(0.34, 0.03, 0.05);
    return base * light * (0.55 + 0.45 * cos(ph) * 0.5 + 0.5) + vec3(0.55, 0.10, 0.12) * sheen * (length(light) * 0.5);
}

void main()
{
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    vec4 a = uInvVP * vec4(ndc, -1.0, 1.0), b = uInvVP * vec4(ndc, 1.0, 1.0);
    vec3 rw = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 ro = vec3(dot(uCamPos - uFloorPoint, uFloorX), dot(uCamPos - uFloorPoint, uFloorNormal), dot(uCamPos - uFloorPoint, uFloorZ));
    vec3 rd = vec3(dot(rw, uFloorX), dot(rw, uFloorNormal), dot(rw, uFloorZ));
    vec3 V = -rd;
    gCam = ro;

    float t = 1e9; vec3 col = vec3(0.0); bool hit = false;
    float ow = openW() * 0.5;

    // --- the proscenium, masking, valance and curtains: planes just in front of the screen
    if (rd.z < 0.0) {
        // curtains (z = 0.045): two panels that slide apart and bunch at the sides
        float tc = (0.045 - ro.z) / rd.z;
        vec3 p = ro + rd * tc;
        if (tc > 0.0 && p.y > kStageY && p.y < kOpenY1 + 0.14) {
            float c = clamp(uCurtain, 0.0, 1.0);
            float inner = mix(-0.05, ow - 0.02, c);         // the panels' inner edges (overlapping when closed)
            float outer = mix(ow + 0.12, ow + 0.50, c);     // outer edges (gathered when open)
            float ax = abs(p.x);
            if (ax > inner && ax < outer) {
                float width = outer - inner;
                float u = (ax - inner) / width;
                col = velvet(u, width, p, V);
                col *= 0.7 + 0.3 * smoothstep(kStageY, kStageY + 0.2, p.y);   // darker at the hem
                float fringe = 1.0 - smoothstep(0.0, 0.02, p.y - kStageY - 0.03);
                col = mix(col, vec3(0.55, 0.40, 0.15) * (house() * 0.6 + screenLight(p, vec3(0, 0, 1)) + 0.03), fringe);
                t = tc; hit = true;
            }
        }
        // valance (z = 0.05): across the top, with a scalloped lower edge and gold trim
        float tv = (0.05 - ro.z) / rd.z;
        p = ro + rd * tv;
        float bottom = kOpenY1 - 0.06 + 0.035 * abs(sin(p.x * 3.1416 * 3.0));
        if (tv > 0.0 && tv < t && abs(p.x) < ow + 0.55 && p.y > bottom && p.y < kOpenY1 + 0.30) {
            col = velvet(fract(p.x * 0.8), 1.0, p, V) * 0.9;
            float trim = 1.0 - smoothstep(0.0, 0.012, abs(p.y - bottom - 0.02));
            col = mix(col, vec3(0.70, 0.52, 0.20) * (house() * 0.7 + screenLight(p, vec3(0, 0, 1)) * 1.5 + 0.05), trim);
            t = tv; hit = true;
        }
        // proscenium face (z = 0.035) with its opening, and the masking inside it (z = 0.02)
        float tp = (0.035 - ro.z) / rd.z;
        p = ro + rd * tp;
        if (tp > 0.0 && tp < t && abs(p.x) < wallX() && p.y < kCeil && p.y > kStageY) {
            bool inOpening = abs(p.x) < ow && p.y > kOpenY0 && p.y < kOpenY1;
            if (!inOpening) {
                float edge = max(abs(p.x) - ow, max(kOpenY0 - p.y, p.y - kOpenY1));
                float trim = 1.0 - smoothstep(0.0, 0.01, abs(edge - 0.04));
                vec3 alb = mix(vec3(0.12, 0.03, 0.035), vec3(0.72, 0.54, 0.22), trim);
                vec3 glow = picAvg() * 0.3 * exp(-edge * 4.0);   // the screen's spill on the frame
                // a sconce either side of the stage: a warm wash up the wall, never quite off
                vec2 sc = vec2(abs(p.x) - (ow + 0.75), p.y - 0.85);   // outside the gathered curtains
                // mostly upward, with a soft spill below the lamp (no hard edge)
                float wash = exp(-sc.x * sc.x * 30.0) * exp(-max(sc.y, 0.0) * 2.2) * exp(min(sc.y, 0.0) * 7.0);
                float lamp = exp(-dot(sc, sc) * 3000.0);
                float level = 0.18 + 0.82 * uHouse;
                col = alb * (house() * 0.5 + glow + vec3(1.0, 0.75, 0.45) * wash * 0.9 * level + vec3(0.006));
                col += vec3(1.0, 0.82, 0.55) * lamp * 1.6 * level;
                t = tp; hit = true;
            }
        }
        float tm = (0.02 - ro.z) / rd.z;
        p = ro + rd * tm;
        if (tm > 0.0 && tm < t && abs(p.x) < ow && p.y > kOpenY0 && p.y < kOpenY1) {
            bool open = p.x > uMask.x && p.x < uMask.y && p.y > uMask.z && p.y < uMask.w;
            if (!open) { col = vec3(0.008, 0.008, 0.009) + picAvg() * 0.01; t = tm; hit = true; }   // black velvet masking
        }
    }

    // --- the auditorium: stadium rows of 3D seats and the stepped, carpeted floor (fixed in
    //     the room; the camera sits in a row). Ray-marched through the audience's bounding box.
    {
        vec3 bmin = vec3(-wallX(), kAudY - 0.01, kRow0Z - kRowD * 0.5);
        vec3 bmax = vec3(wallX(), rowFloor(kRows - 1) + 0.24, kRow0Z + kRowD * (float(kRows) - 0.5));
        vec3 inv = 1.0 / rd;
        vec3 t0 = (bmin - ro) * inv, t1 = (bmax - ro) * inv;
        vec3 tmin = min(t0, t1), tmax = max(t0, t1);
        float tn = max(max(tmin.x, tmin.y), tmin.z), tf = min(min(tmax.x, tmax.y), tmax.z);
        if (tf > max(tn, 0.0)) {
            float tt = max(tn, 0.0) + 1e-4, tEnd = min(tf, t);
            int part = -1, row = 0;
            bool found = false;
            int maxSteps = uMarchRef ? 4000 : 110;
            float stepScale = uMarchRef ? 0.2 : 0.8;
            for (int i = 0; i < 4000; ++i) {
                if (i >= maxSteps) break;
                vec3 q = ro + rd * tt;
                float d = audSdf(q, part, row);
                if (d < 0.0006 * max(tt, 1.0)) { found = true; break; }
                tt += max(d * stepScale, uMarchRef ? 0.0002 : 0.0006);
                if (tt > tEnd) break;
            }
            if (found && tt < t) {
                vec3 q = ro + rd * tt;
                int pp, rr;
                const vec2 e = vec2(0.0015, -0.0015);
                vec3 n = normalize(e.xyy * audSdf(q + e.xyy, pp, rr) + e.yyx * audSdf(q + e.yyx, pp, rr) +
                                   e.yxy * audSdf(q + e.yxy, pp, rr) + e.xxx * audSdf(q + e.xxx, pp, rr));
                vec3 alb = part == 2 ? vec3(0.035, 0.035, 0.04)                       // armrests: dark plastic
                         : part == 3 ? vec3(0.07, 0.02, 0.025)                        // carpeted steps
                         : vec3(0.26, 0.035, 0.045) * (part == 1 ? 0.8 : 1.0);         // red fabric
                vec3 light = house() * 0.60 + seatLight(q, n) + vec3(0.006);
                col = alb * light;
                if (part == 2) col += pow(max(dot(reflect(rd, n), normalize(vec3(0.0, 0.5, 0.0) - q)), 0.0), 30.0) * picAvg() * 0.25;
                if (part == 0 || part == 1) col += alb * pow(1.0 - abs(dot(n, -rd)), 3.0) * (picAvg() * 0.3 + house() * 0.1);   // fabric sheen
                t = tt; hit = true;
            }
        }
    }

    // --- aisle step lights: on each step's riser at the edges of the two aisles
    vec3 glow = vec3(0.0);
    for (int k = 0; k < kRows; ++k) {
        float zr = kRow0Z + (float(k) - 0.5) * kRowD + 0.004;     // the riser in front of row k
        if (zr > ro.z - 0.05) break;
        float yk = rowFloor(k) - 0.012;
        for (int s = 0; s < 4; ++s) {
            float side = s < 2 ? -1.0 : 1.0;
            float edge = (s % 2 == 0) ? -0.075 : 0.075;
            vec3 L = vec3(side * kAisleX + edge, yk, zr);
            vec3 d = L - ro;
            float along = dot(d, rd);
            if (along <= 0.0 || along > t + 0.01) continue;
            float perp = length(d - rd * along) / along;
            glow += vec3(1.0, 0.72, 0.35) * exp(-perp * perp / 0.000012) * 0.7;
        }
    }

    // --- the projector beam: from the booth behind you to the picture, carrying its colours
    vec3 beam = vec3(0.0);
    if (uBeam > 0.0 && rd.z < 0.0) {
        vec3 P0 = vec3(0.0, 1.45, 5.9);   // the projection booth, in the back wall
        float tEnd = min(t, -ro.z / rd.z);
        const int N = 14;
        float dt = tEnd / float(N);
        for (int i = 0; i < N; ++i) {
            float s = (float(i) + hash2(gl_FragCoord.xy + float(i) * 7.0)) * dt;
            vec3 q = ro + rd * s;
            vec3 dir = q - P0;
            if (dir.z >= -1e-3) continue;
            float k = -P0.z / dir.z;                      // where this ray from the projector meets the screen
            vec2 sp = P0.xy + dir.xy * k;
            if (abs(sp.x) > uAspect * 0.5 || sp.y < 0.0 || sp.y > 1.0) continue;
            float spread = length(dir) / max(length(dir * k), 1e-3);
            float dust = pow(hash3(floor(q * 180.0 + vec3(0.0, uTime * 3.0, 0.0))), 120.0) * 2.5;
            float nearScreen = smoothstep(0.0, 0.35, q.z);   // fades out just before the screen
            beam += picAt(sp, 6.5) * (spread * spread) * (1.0 + dust) * nearScreen * dt;
        }
        beam *= uBeam * (0.03 + 0.38 * uFog);   // faint in clear air; the haze makes it show
    }

    // haze between you and the screen: a glow around the picture in its colours, and a veil
    vec3 haze = vec3(0.0);
    if (uFog > 0.0) {
        vec3 toC = normalize(vec3(0.0, 0.5, 0.0) - ro);
        float cosA = max(dot(rd, toC), 0.0);
        float halo = pow(cosA, 40.0) * 0.9 + pow(cosA, 8.0) * 0.25;
        float dist = hit ? t : 6.0;
        haze = (picAvg() * halo * 0.55 + house() * 0.05 + vec3(0.01)) * uFog * (1.0 - exp(-dist * 0.35));
    }
    // Looking straight at the picture, the haze softens it rather than bleaching it.
    bool onPicture = false;
    if (!hit && rd.z < 0.0) {
        vec3 sp = ro + rd * (-ro.z / rd.z);
        onPicture = abs(sp.x) < uAspect * 0.5 && sp.y > 0.0 && sp.y < 1.0;
    }
    if (onPicture) { beam *= 0.35; haze *= 0.35; }
    vec3 add = glow + beam + haze;
    if (hit) fragColor = vec4(col + add, 1.0);
    else fragColor = vec4(add, 0.0);   // premultiplied: only light, nothing covered
}
