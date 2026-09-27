#version 330 core
// Pass 3: CRT composite, drawn over the whole video area.
//
// Coordinate spaces (target pixels, origin top-left):
//   px     - this fragment
//   uTube  - the simulated glass. By default the visible picture rect, so letterbox /
//            pillarbox bars stay untouched; with "include bars" the whole video area.
//   uImg   - the visible picture rect (content outside it is black bar)
//   uSrc   - the uv window of the full picture shown inside uImg
//
// Stage order (mirrors a real chain: source signal -> set's electronics -> tube):
//   1. tube geometry: curvature + rounded outline
//   2. scanline geometry (the pixel-beam style samples at beam centres)
//   3. SIGNAL: VHS timebase distortions, chroma aberration, tape bandwidth, chroma delay,
//      composite dot crawl / rainbow, colour bleed, tracking/head/dropout/laser-rot noise
//   4. picture controls
//   5. scanlines, phosphor mask, glow, bloom, vignette, noise, flicker
//
// Signal artefacts are measured in picture lines (480, NTSC) and picture width, so they
// look the same on 4:3, 16:9, ultrawide and vertical sources and bend with the tube.
out vec4 fragColor;

uniform sampler2D uImage;
uniform sampler2D uBlur;
uniform vec2  uViewport;
uniform vec4  uTube;
uniform vec4  uImg;
uniform vec4  uSrc;
uniform float uTime;
uniform float uFrameRand;
uniform bool  uBypass;
uniform float uSplit;
uniform bool  uHasBlur;

uniform float uScanStrength;
uniform float uScanWidth;
uniform float uLines;
uniform int   uScanType;        // 0 soft, 1 sharp, 2 dynamic, 3 interlaced, 4 VGA double-scan, 5 pixel beam,
                                // 6-9 beam reconstruction (geom-, lottes-, easymode-, hyllian-style), 10 sine (MAME HLSL-style)
uniform float uSrcLod;          // mip level that brings the picture down to scanline resolution
uniform sampler2D uImageFull;   // full-resolution picture (bypass / "original" side of compare)
uniform int   uPixelMode;       // lowered resolution: -1 off, 0 hard, 1 sharp (AA edges), 2 soft
uniform vec2  uPixelSize;       // lowered resolution in texels
uniform vec2  uTexelPx;         // screen pixels per lowered texel
uniform int   uColorDepth;      // 0 full, 1-6 bits per channel sets, 7 Game Boy, 8 CGA, 9 EGA
uniform int   uDither;          // 0 none, 1 Bayer 4x4, 2 Bayer 8x8
uniform vec2  uGridSize;        // picture pixel grid (lowered or native) for dither alignment
uniform int   uVideoStd;        // 0 NTSC, 1 PAL
uniform int   uTransType;       // set moments: 0 none, 1 power on, 2 power off
uniform float uTransT;          // progress 0..1
uniform float uStatic;          // 0..1 static between channels/items
uniform sampler2D uOsd;         // VCR on-screen display text (premultiplied RGBA)
uniform float uOsdAlpha;
uniform vec4  uOsdRect;         // in picture uv: x, y, w, h
uniform int   uMaskType;
uniform float uMaskStrength;
uniform float uMaskScale;
uniform float uCurvature;
uniform float uCorner;
uniform float uOverscan;
uniform float uBloom;
uniform float uGlow;
uniform float uChroma;
uniform float uBleed;
uniform float uNoise;
uniform float uFlicker;
uniform float uVignette;
uniform float uBrightness;
uniform float uContrast;
uniform float uSaturation;
uniform float uWarmth;

uniform float uVhsJitter;
uniform float uVhsTracking;
uniform float uVhsHead;
uniform float uVhsChroma;
uniform float uVhsDrop;
uniform float uFilmGrain;
uniform float uGateWeave;
uniform float uFilmFlicker;
uniform float uFilmDamage;
uniform float uVhsSoft;
uniform float uDotCrawl;
uniform float uRainbow;
uniform float uLaserRot;

const vec3 kLuma = vec3(0.299, 0.587, 0.114);
// Signal geometry: NTSC 480 visible lines / 59.94 fields/s; PAL 576 lines / 50 fields/s.
float signalLines() { return uVideoStd == 1 ? 576.0 : 480.0; }
float fieldRate() { return uVideoStd == 1 ? 50.0 : 59.94; }
const float kSignalSamples = 640.0; // luma samples across the picture for composite effects

// Mip level for picture fetches: < 0 = automatic (normal sampling); the beam styles set
// it so the picture is read at scanline resolution, like a low-resolution signal.
float gLod = -1.0;
// Set on the CRT side only: the bypass / "original" side always shows full resolution.
bool gPixelate = false;

// Ordered (Bayer) dither threshold in [0,1): bits of x^y and y interleaved, reversed.
float bayer(vec2 cell, int n)
{
    int size = 1 << n;
    ivec2 i = ivec2(mod(cell, float(size)));
    int v = 0;
    for (int b = 0; b < 3; ++b) {
        if (b >= n) break;
        int xb = (i.x >> b) & 1, yb = (i.y >> b) & 1;
        v = (v << 2) | (((xb ^ yb) << 1) | yb);
    }
    return (float(v) + 0.5) / float(size * size);
}

vec3 nearestOf(vec3 c, int count, int which)
{
    // which 0: CGA (black, cyan, magenta, white); 1: EGA 16 colours
    const vec3 cga[4] = vec3[4](vec3(0.0), vec3(0.333, 1.0, 1.0), vec3(1.0, 0.333, 1.0), vec3(1.0));
    const vec3 ega[16] = vec3[16](vec3(0.0), vec3(0.0, 0.0, 0.667), vec3(0.0, 0.667, 0.0), vec3(0.0, 0.667, 0.667),
                                  vec3(0.667, 0.0, 0.0), vec3(0.667, 0.0, 0.667), vec3(0.667, 0.333, 0.0), vec3(0.667),
                                  vec3(0.333), vec3(0.333, 0.333, 1.0), vec3(0.333, 1.0, 0.333), vec3(0.333, 1.0, 1.0),
                                  vec3(1.0, 0.333, 0.333), vec3(1.0, 0.333, 1.0), vec3(1.0, 1.0, 0.333), vec3(1.0));
    vec3 best = vec3(0.0);
    float bd = 1e9;
    for (int k = 0; k < 16; ++k) {
        if (k >= count) break;
        vec3 q = which == 0 ? cga[k] : ega[k];
        vec3 d = c - q;
        float dist = dot(d * d, vec3(0.30, 0.59, 0.11));
        if (dist < bd) { bd = dist; best = q; }
    }
    return best;
}

// Reduced colour depth, with optional ordered dithering aligned to the picture's pixels.
vec3 quantize(vec3 c, vec2 uv)
{
    float t = 0.0;
    if (uDither > 0) t = bayer(floor(uv * uGridSize), uDither == 1 ? 2 : 3) - 0.5;
    if (uColorDepth <= 6) {
        vec3 bits = uColorDepth == 1 ? vec3(5.0) : uColorDepth == 2 ? vec3(4.0) : uColorDepth == 3 ? vec3(3.0)
                  : uColorDepth == 4 ? vec3(3.0, 3.0, 2.0) : uColorDepth == 5 ? vec3(2.0) : vec3(1.0);
        vec3 L = exp2(bits) - 1.0;
        return clamp(floor(c * L + 0.5 + t), 0.0, 255.0) / L;
    }
    if (uColorDepth == 7) {   // Game Boy (DMG) greens, by luminance
        const vec3 gb[4] = vec3[4](vec3(0.059, 0.220, 0.059), vec3(0.188, 0.384, 0.188),
                                   vec3(0.545, 0.675, 0.059), vec3(0.608, 0.737, 0.059));
        float lv = clamp(floor(dot(c, vec3(0.299, 0.587, 0.114)) * 3.0 + 0.5 + t), 0.0, 3.0);
        return gb[int(lv)];
    }
    if (uColorDepth == 8) return nearestOf(c + t * 0.33, 4, 0);
    return nearestOf(c + t * 0.20, 16, 1);
}

vec3 fetch(sampler2D tex, vec2 p)
{
    vec2 t = (p - uImg.xy) / uImg.zw;
    vec2 edge = min(p - uImg.xy, uImg.xy + uImg.zw - p);
    float cover = clamp(min(edge.x, edge.y) + 0.5, 0.0, 1.0);
    if (cover <= 0.0) return vec3(0.0);
    vec2 uv = uSrc.xy + clamp(t, 0.0, 1.0) * uSrc.zw;
    if (gPixelate && uPixelMode >= 0 && uPixelMode < 2) {
        // Bigger pixels: sample texel centres. "Sharp" blends across a one-screen-pixel
        // band at each texel boundary, so blocks stay crisp without shimmering.
        vec2 x = uv * uPixelSize - 0.5;
        vec2 n = floor(x);
        vec2 a = fract(x);
        if (uPixelMode == 0) a = step(0.5, a);
        else a = clamp((a - 0.5) * max(uTexelPx, vec2(1e-3)) + 0.5, 0.0, 1.0);
        uv = (n + a + 0.5) / uPixelSize;
    }
    vec3 c = gLod < 0.0 ? texture(tex, uv).rgb : textureLod(tex, uv, gLod).rgb;
    if (gPixelate && uColorDepth > 0) c = quantize(c, uv);
    return c * cover;
}

vec3 rgb2yiq(vec3 c) { return mat3(0.299, 0.596, 0.211, 0.587, -0.274, -0.523, 0.114, -0.322, 0.312) * c; }
vec3 yiq2rgb(vec3 c) { return mat3(1.0, 1.0, 1.0, 0.956, -0.272, -1.106, 0.621, -0.647, 1.703) * c; }

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

vec3 phosphorMask(vec2 pos)
{
    pos = floor(pos / uMaskScale);
    float dark = 1.0 - uMaskStrength;
    vec3 m = vec3(dark);
    if (uMaskType == 1) {
        float x = fract(pos.x / 3.0);
        if (x < 0.333) m.r = 1.0; else if (x < 0.666) m.g = 1.0; else m.b = 1.0;
    } else if (uMaskType == 2) {
        pos.x += pos.y * 3.0;
        float x = fract(pos.x / 6.0);
        if (x < 0.333) m.r = 1.0; else if (x < 0.666) m.g = 1.0; else m.b = 1.0;
    } else if (uMaskType == 3) {
        float odd = fract(pos.x / 6.0) < 0.5 ? 1.0 : 0.0;
        float line = fract((pos.y + odd) / 2.0) < 0.5 ? dark : 1.0;
        float x = fract(pos.x / 3.0);
        if (x < 0.333) m.r = 1.0; else if (x < 0.666) m.g = 1.0; else m.b = 1.0;
        m *= line;
    }
    return m;
}

// Signal stage + picture controls at content position p (px). Used once per pixel by
// the classic styles, and once per contributing scanline by the beam-reconstruction
// styles, so tape/disc/composite artefacts travel through the beams like real signal.
vec3 sourceColor(vec2 p, vec2 qc, vec2 px, float unit)
{
    // ---- 3. signal ---------------------------------------------------------
    vec2 picSize = uImg.zw / uSrc.zw;               // full picture size in px
    vec2 picOrigin = uImg.xy - uSrc.xy * picSize;    // full picture top-left in px
    float sv = (p.y - picOrigin.y) / picSize.y;      // 0..1 down the picture
    float sline = floor(sv * signalLines());
    float field = floor(uTime * fieldRate());
    float sstep = picSize.x / kSignalSamples;

    vec2 pw = p;
    float band = 0.0, head = 0.0;
    if (uVhsJitter > 0.0) {
        pw.x += ((hash12(vec2(sline, field)) - 0.5) * 0.004
                 + sin(sv * 23.0 + uTime * 1.7) * 0.0015) * uVhsJitter * picSize.x;
    }
    if (uVhsTracking > 0.0) {
        float centre = fract(uTime * 0.045) * 1.4 - 0.2;   // rolls top -> bottom
        float d = (sv - centre) / 0.045;
        band = exp(-d * d) * uVhsTracking;
        pw.x += (hash12(vec2(sline * 1.37, field)) - 0.5) * 0.06 * band * picSize.x;
    }
    if (uVhsHead > 0.0) {
        head = smoothstep(0.955, 1.0, sv) * uVhsHead;
        pw.x -= head * (0.035 + 0.02 * hash12(vec2(sline, field * 3.1))) * picSize.x;
    }

    vec2 off = (qc * 0.8 + vec2(0.35, 0.0)) * uChroma * unit;
    vec3 base = (uChroma > 0.0)
        ? vec3(fetch(uImage, pw + off).r, fetch(uImage, pw).g, fetch(uImage, pw - off).b)
        : fetch(uImage, pw);
    vec3 yiq = rgb2yiq(base);

    if (uVhsChroma > 0.0) {
        // Chroma recorded with a delay: colour trails to the right of detail.
        yiq.yz = rgb2yiq(fetch(uImage, pw - vec2(uVhsChroma * sstep * 7.0, 0.0))).yz;
    }
    if (uVhsSoft > 0.0 || uDotCrawl > 0.0 || uRainbow > 0.0) {
        // Composite model. The NTSC subcarrier sits ~4 luma samples per cycle here.
        // Its phase inverts on every line and advances a quarter cycle per field,
        // a 4-field sequence, so the dots travel (crawl) instead of just flickering.
        // PAL: 3/4 cycle per line and an 8-field sequence give its diagonal pattern; the
        // V component switches sign every line (the "Phase Alternating Line").
        bool pal = uVideoStd == 1;
        float phBase = pal ? sline * 4.7124 + mod(field, 8.0) * 0.7854 : sline * 3.1416 + mod(field, 4.0) * 1.5708;
        float vsw = (pal && mod(sline, 2.0) >= 1.0) ? -1.0 : 1.0;
        float sx = sstep * (1.0 + uVhsSoft * 2.0);
        float ys = 0.0, ws = 0.0, cw = 0.0;
        vec2 cs = vec2(0.0), demod = vec2(0.0);
        float ycentre = yiq.x;
        for (int i = -3; i <= 3; ++i) {
            float fi = float(i);
            vec2 tp = pw + vec2(fi * sx, 0.0);
            vec3 s = rgb2yiq(fetch(uImage, tp));
            float w = exp(-0.5 * fi * fi / 2.25);
            float ph = (tp.x - picOrigin.x) / sstep * 1.5708 + phBase;
            ys += s.x * w; ws += w;
            // Luma detail multiplied by the carrier and low-passed = false colour.
            demod += (s.x - ycentre) * vec2(cos(ph), sin(ph) * vsw) * w;
            // Chroma has ~1/3 of luma bandwidth: gather it over a 3x wider span.
            vec3 sc = rgb2yiq(fetch(uImage, pw + vec2(fi * sx * 3.0, 0.0)));
            cs += sc.yz * w; cw += w;
        }
        float yb = ys / ws;
        vec2 cb = cs / cw;
        float ph0 = (pw.x - picOrigin.x) / sstep * 1.5708 + phBase;
        vec2 carrier = vec2(cos(ph0), sin(ph0) * vsw);
        // Rainbow: fine luma detail decoded as colour.
        yiq.yz += demod / ws * uRainbow * 4.0;
        // Dot crawl: chroma the luma notch filter could not remove (colour edges).
        yiq.x += dot(yiq.yz - cb, carrier) * uDotCrawl * 1.6;
        yiq.x = mix(yiq.x, yb, uVhsSoft * 0.85);
        yiq.yz = mix(yiq.yz, cb, uVhsSoft * 0.6);
        if (pal) {
            // Hanover bars: a residual hue error that flips sign line by line.
            float a = vsw * 0.22 * clamp(uRainbow + uDotCrawl, 0.0, 1.0);
            yiq.yz = mat2(cos(a), sin(a), -sin(a), cos(a)) * yiq.yz;
        }
    }
    if (uBleed > 0.0) {
        float bstep = sstep * (0.5 + uBleed * 1.5);
        vec3 acc = vec3(0.0); float wsum = 0.0;
        for (int i = 0; i < 6; ++i) {
            float w = exp(-float(i) * 0.45);
            acc += rgb2yiq(fetch(uImage, pw - vec2(float(i) * bstep, 0.0))) * w;
            wsum += w;
        }
        yiq.yz = mix(yiq.yz, acc.yz / wsum, clamp(uBleed * 1.2, 0.0, 1.0));
    }
    base = yiq2rgb(yiq);

    if (uOsdAlpha > 0.0) {
        // VCR on-screen display: added to the output signal after tape playback, so it is
        // steady (no tape jitter) but still goes through the TV: noise, scanlines, mask.
        vec2 puv = (p - picOrigin) / picSize;
        vec2 l = (puv - uOsdRect.xy) / uOsdRect.zw;
        if (all(greaterThanEqual(l, vec2(0.0))) && all(lessThanEqual(l, vec2(1.0)))) {
            vec4 o = texture(uOsd, l);
            vec4 o2 = texture(uOsd, l - vec2(0.006, 0.0));   // slight trailing smear, as on a VCR
            o = max(o, o2 * 0.6);
            base = base * (1.0 - o.a * uOsdAlpha) + o.rgb * uOsdAlpha;
        }
    }

    bool insidePic = sv >= 0.0 && sv <= 1.0 && pw.x >= uImg.x && pw.x <= uImg.x + uImg.z;
    if (insidePic) {
        float n1 = hash12(vec2(floor((pw.x - picOrigin.x) / (picSize.x / 220.0)), sline + field * 17.0));
        if (band > 0.0) {
            base = mix(base, vec3(0.92), band * step(0.9, n1));
            base += (n1 - 0.5) * band * 0.35;
        }
        if (head > 0.0)
            base = mix(base, vec3(hash12(floor(px) + field)), head * 0.35);
        if (uVhsDrop > 0.0 && hash12(vec2(sline, field * 0.37)) > 1.0 - uVhsDrop * 0.025) {
            float x0 = hash12(vec2(sline * 2.1, field));
            float len = 0.03 + 0.15 * hash12(vec2(field, sline));
            float xs = (pw.x - picOrigin.x) / picSize.x;
            if (xs > x0 && xs < x0 + len) base = mix(base, vec3(0.85 + 0.15 * n1), 0.85);
        }
        if (uLaserRot > 0.0) {
            float cell = picSize.y / 150.0;
            vec2 cid = floor((pw - picOrigin) / cell);
            float h = hash12(cid + floor(uTime * 7.0) * vec2(3.1, 7.7));
            if (h > 1.0 - uLaserRot * 0.012) {
                vec2 f = fract((pw - picOrigin) / cell) - 0.5;
                float s = smoothstep(0.5, 0.1, length(f));
                vec3 speck = mix(vec3(1.0), vec3(hash12(cid + 1.0), hash12(cid + 2.0), hash12(cid + 3.0)), 0.6);
                base = mix(base, speck, s * 0.9);
            }
        }
    }
    vec3 col = base;

    // picture controls
    col = (col - 0.5) * uContrast + 0.5;
    col *= uBrightness;
    col = mix(vec3(dot(col, kLuma)), col, uSaturation);
    col *= vec3(1.0 + 0.08 * uWarmth, 1.0, 1.0 - 0.10 * uWarmth);
    col = max(col, 0.0);
    return col;
}

// Picture controls only, on an unprocessed sample (used for horizontal neighbours).
vec3 plainColor(vec2 p)
{
    vec3 col = fetch(uImage, p);
    col = (col - 0.5) * uContrast + 0.5;
    col *= uBrightness;
    col = mix(vec3(dot(col, kLuma)), col, uSaturation);
    col *= vec3(1.0 + 0.08 * uWarmth, 1.0, 1.0 - 0.10 * uWarmth);
    return max(col, 0.0);
}

void main()
{
    vec2 px = vec2(gl_FragCoord.x, uViewport.y - gl_FragCoord.y);
    bool flatSide = uBypass || (uSplit >= 0.0 && px.x < uSplit);

    vec3 col;
    if (flatSide) {
        col = fetch(uImageFull, px);
    } else {
        // ---- 1. tube geometry ------------------------------------------------------
        vec2 tuv = (px - uTube.xy) / uTube.zw;
        vec2 c = tuv * 2.0 - 1.0;
        vec2 hs = uTube.zw * 0.5;
        float R = length(hs);
        vec2 E = hs / R;
        vec2 P = c * E;
        float k = uCurvature;
        vec2 q = c * (1.0 + k * dot(P, P)) / (1.0 + k * E * E);
        float filmFrame = floor(uTime * 24.0);   // film runs at 24 frames per second
        if (uGateWeave > 0.0) {
            // the frame drifts in the gate: a slow wander plus a small jump each frame
            vec2 w = vec2(hash12(vec2(filmFrame, 1.3)) - 0.5, hash12(vec2(filmFrame, 7.9)) - 0.5) * 0.5
                   + vec2(sin(uTime * 0.9), sin(uTime * 0.7 + 1.0)) * 0.5;
            q += w * uGateWeave * vec2(0.004, 0.007);
        }

        vec2 qpx = abs(q) * hs;
        float rad = uCorner * min(hs.x, hs.y);
        vec2 dd = qpx - (hs - vec2(rad));
        float sd = length(max(dd, 0.0)) + min(max(dd.x, dd.y), 0.0) - rad;
        float aa = max(fwidth(sd), 1e-3);
        float tubeMask = 1.0 - smoothstep(-aa, aa, sd);
        if (tubeMask <= 0.0) {
            col = vec3(0.0);
        } else {
            gPixelate = true;
            vec2 qc = q * (1.0 - uOverscan);
            // Set moments: the raster opens from a bright line (power on) or collapses to a
            // line and then a dot (power off). The picture is squeezed with it.
            float transMask = 1.0, transBoost = 1.0, transWhite = 0.0, transLevel = 1.0;
            if (uTransType == 1) {
                float sv = max(smoothstep(0.0, 0.55, uTransT), 0.006);
                qc.y /= sv;
                transBoost = 1.0 + 2.5 * (1.0 - smoothstep(0.0, 0.8, uTransT));
                transWhite = 1.0 - smoothstep(0.0, 0.35, uTransT);
            } else if (uTransType == 2) {
                float sv = 1.0 - 0.995 * smoothstep(0.0, 0.30, uTransT);
                float sh = 1.0 - 0.992 * smoothstep(0.30, 0.50, uTransT);
                qc.y /= sv;
                qc.x /= sh;
                transBoost = 1.0 + 3.0 * smoothstep(0.0, 0.30, uTransT);
                transWhite = smoothstep(0.10, 0.35, uTransT);
                transLevel = 1.0 - smoothstep(0.50, 1.0, uTransT);
            }
            if (uTransType > 0 && (abs(qc.y) > 1.0 || abs(qc.x) > 1.0)) transMask = 0.0;
            vec2 p = uTube.xy + (qc * 0.5 + 0.5) * uTube.zw;
            float unit = max(uTube.z, uTube.w) / 1000.0;

            // ---- 2. scanline geometry ---------------------------------------------
            float lines = uLines;
            if (uScanType == 4 && uTube.w / (lines * 2.0) >= 2.5) lines *= 2.0;   // double-scan when it fits
            float ly = (qc.y * 0.5 + 0.5) * lines;
            float cols = lines * uTube.z / uTube.w;
            float lx = (qc.x * 0.5 + 0.5) * cols;
            if (uScanType == 5 && lines > 0.0) {
                // Pixel beam: the gun is fed one sample per low-res pixel.
                p = uTube.xy + vec2((floor(lx) + 0.5) / cols, (floor(ly) + 0.5) / lines) * uTube.zw;
            }

            // ---- 3+4. signal and picture controls ------------------------------------
            float field = floor(uTime * fieldRate());
            vec3 col0 = vec3(0.0);
            bool beamStyle = uScanType >= 6 && uScanType <= 9 && uScanStrength > 0.0 && lines > 0.0;
            if (!beamStyle) col0 = sourceColor(p, qc, px, unit);
            // Static between channels / playlist items (before the tube, so it gets scanlines).
            float snow = hash12(floor(px / 1.5) + vec2(uFrameRand * 917.0, uFrameRand * 433.0));
            snow = snow * (0.85 + 0.15 * sin(px.y * 0.05 + uTime * 40.0));
            if (uStatic > 0.0) col0 = mix(col0, vec3(snow), uStatic);
            col = col0;
            // ---- 5. tube ------------------------------------------------------------
            if (beamStyle) {
                // ---- beam reconstruction -------------------------------------------------
                // Every output pixel sums the light of the nearest source scanlines. Each
                // line is a beam whose vertical profile exp(-(d/sigma)^shape) widens with the
                // line's brightness; the sum happens in linear light. This is what makes
                // bright lines swell into the gaps while dark lines stay thin. The picture is
                // read at scanline resolution (uSrcLod). Independent implementation of the
                // general technique used by well-known shaders; not a port of any of them.
                float sigDark, sigBright, shape, sharp, gIn, gOut, hardPix;
                int kMin, kMax;
                if (uScanType == 6) {        // geom-style: moderate beams, mild sharpening
                    sigDark = 0.22; sigBright = 0.37; shape = 2.0; sharp = 0.45; gIn = 2.4; gOut = 2.2; hardPix = 0.0; kMin = 0; kMax = 1;
                } else if (uScanType == 7) { // lottes-style: constant beam, horizontal pixel gaussian
                    sigDark = 0.29; sigBright = 0.29; shape = 2.0; sharp = 0.0; gIn = 2.2; gOut = 2.2; hardPix = -3.0; kMin = -1; kMax = 2;
                } else if (uScanType == 8) { // easymode-style: very thin dark lines, fat bright ones
                    sigDark = 0.13; sigBright = 0.36; shape = 2.4; sharp = 0.8; gIn = 2.2; gOut = 2.2; hardPix = 0.0; kMin = 0; kMax = 1;
                } else {                     // hyllian-style: flat-topped beams, hard gaps, crisp
                    sigDark = 0.27; sigBright = 0.40; shape = 4.0; sharp = 1.2; gIn = 2.4; gOut = 2.2; hardPix = 0.0; kMin = 0; kMax = 1;
                }
                float widthScale = mix(0.8, 1.25, uScanWidth);
                gLod = uSrcLod;
                float lyc = ly - 0.5;                    // 0 at the centre of line 0
                float n0 = floor(lyc);
                float fracY = lyc - n0;
                float colW = uTube.w / lines;            // one source pixel (square) in px
                vec3 acc = vec3(0.0);
                vec3 linA = vec3(0.0), linB = vec3(0.0);
                for (int k = kMin; k <= kMax; ++k) {
                    float ln = n0 + float(k);
                    if (ln < 0.0 || ln > lines - 1.0) continue;         // above/below the picture
                    float yq = (ln + 0.5) / lines * 2.0 - 1.0;
                    vec2 pl = uTube.xy + vec2(qc.x * 0.5 + 0.5, yq * 0.5 + 0.5) * uTube.zw;
                    vec3 c = sourceColor(pl, vec2(qc.x, yq), px, unit);
                    vec3 lin;
                    if (hardPix < 0.0) {
                        // Horizontal gaussian across the three nearest source pixels; the
                        // signal-stage result rides along as a per-line difference.
                        vec3 delta = c - plainColor(pl);
                        float c0 = floor(lx);
                        vec3 hs = vec3(0.0); float hw = 0.0;
                        for (int j = -1; j <= 1; ++j) {
                            float cc = c0 + float(j) + 0.5;
                            float dx = lx - cc;
                            float w = exp2(hardPix * dx * dx);
                            vec2 pc = vec2(uTube.x + cc / cols * uTube.z, pl.y);
                            hs += pow(max(plainColor(pc) + delta, 0.0), vec3(gIn)) * w;
                            hw += w;
                        }
                        lin = hs / hw;
                    } else {
                        // Lanczos-like sharpening against the neighbouring source pixels.
                        vec3 l = plainColor(pl - vec2(colW, 0.0));
                        vec3 r = plainColor(pl + vec2(colW, 0.0));
                        c = max(c + sharp * (c - 0.5 * (l + r)), 0.0);
                        lin = pow(c, vec3(gIn));
                    }
                    // Beam width per colour channel: a saturated blue beam swells in blue
                    // even though its overall luminance is low, as on a real tube.
                    vec3 chan = clamp(pow(lin, vec3(1.0 / gIn)), 0.0, 1.0);
                    vec3 sig = mix(vec3(sigDark), vec3(sigBright), pow(chan, vec3(0.6))) * widthScale;
                    float d = abs(lyc - ln);
                    acc += lin * exp(-pow(vec3(d) / sig, vec3(shape)));
                    if (k == 0) linA = lin;
                    if (k == 1) linB = lin;
                }
                gLod = -1.0;
                // Keep average brightness: divide (mostly) by the beam's mean over a line.
                float sigAvg = mix(sigDark, sigBright, 0.5) * widthScale;
                float meanBeam = min(1.0, 2.0 * sigAvg * (shape > 3.0 ? 0.906 : 0.886));
                // Partial compensation: full compensation would push merged bright beams
                // past 1.0, where clipping erases the line structure.
                vec3 beamCol = pow(acc / mix(1.0, meanBeam, 0.6), vec3(1.0 / gOut));
                vec3 flatCol = pow(mix(linA, linB, fracY), vec3(1.0 / gIn));
                float pxPerLine = uTube.w / (lines * (1.0 - uOverscan));
                float s = uScanStrength * smoothstep(1.4, 2.8, pxPerLine);
                col = mix(flatCol, beamCol, s);
                if (uStatic > 0.0) col = mix(col, vec3(snow), uStatic);
            } else if (uScanStrength > 0.0 && lines > 0.0) {
                float lyy = ly + ((uScanType == 3) ? 0.5 * mod(field, 2.0) : 0.0);   // alternate fields
                float f = fract(lyy) - 0.5;
                float luma = clamp(dot(col, kLuma), 0.0, 1.0);
                float beam, mean;
                if (uScanType == 1) {            // sharp: flat-topped beam, hard gaps
                    float w = mix(0.30, 0.80, uScanWidth);
                    beam = 1.0 - smoothstep(w * 0.5, w * 0.5 + 0.10, abs(f));
                    mean = clamp(w + 0.1, 0.0, 1.0);
                } else if (uScanType == 4) {     // double-scan: thin gap on every line
                    float g = mix(0.30, 0.08, uScanWidth);           // gap width (fraction of a line)
                    g = max(g, 0.9 * lines * (1.0 - uOverscan) / uTube.w);   // never thinner than ~1 px
                    beam = 1.0 - smoothstep(0.5 - g, 0.5 - g * 0.3, abs(f));
                    mean = 1.0 - g * 0.65;
                } else if (uScanType == 10) {    // MAME HLSL-style: sine-shaped lines, brightness offset
                    float h = mix(2.2, 0.6, uScanWidth);                   // exponent: thin <-> wide
                    float shaped = pow(abs(cos(3.14159265 * f)), h);       // 1 at line centre
                    float offs = 0.15;                                      // gaps never go fully black
                    beam = offs + (1.0 - offs) * shaped;
                    mean = offs + (1.0 - offs) / (1.0 + 0.57 * h);         // average of |cos|^h
                } else {                         // gaussian (soft, dynamic, interlaced, pixel)
                    float sigma = (uScanType == 2)
                        ? mix(0.07, 0.40, uScanWidth) * mix(0.45, 1.7, luma)
                        : mix(0.14, 0.40, uScanWidth) * mix(0.85, 1.3, luma);
                    beam = exp(-0.5 * f * f / (sigma * sigma));
                    mean = clamp(sigma * 2.5066, 0.0, 1.0);
                }
                float pxPerLine = uTube.w / (lines * (1.0 - uOverscan));
                float s = uScanStrength * smoothstep(1.4, 2.8, pxPerLine);
                col *= mix(1.0, beam / mix(1.0, mean, 0.75), s);
                if (uScanType == 5) {            // horizontal beam profile per low-res pixel
                    float fx = fract(lx) - 0.5;
                    float sx = mix(0.25, 0.45, uScanWidth) * mix(0.85, 1.3, luma);
                    float bx = exp(-0.5 * fx * fx / (sx * sx));
                    col *= mix(1.0, bx / mix(1.0, clamp(sx * 2.5066, 0.0, 1.0), 0.75), s * 0.6);
                }
            }

            if (uMaskType > 0 && uMaskStrength > 0.0) {
                vec2 mp = (q * 0.5 + 0.5) * uTube.zw;
                vec3 m = phosphorMask(mp);
                float avg = (uMaskType == 3) ? (1.0 + 2.0 * (1.0 - uMaskStrength)) / 3.0 * (1.0 - 0.5 * uMaskStrength)
                                             : (1.0 + 2.0 * (1.0 - uMaskStrength)) / 3.0;
                col *= m * mix(1.0, 1.0 / avg, 0.65);
            }

            if (uHasBlur && (uGlow > 0.0 || uBloom > 0.0)) {
                vec2 bt = (p - uImg.xy) / uImg.zw;
                vec3 b = texture(uBlur, uSrc.xy + bt * uSrc.zw).rgb;
                col += b * uGlow * 0.55;
                col += max(b - 0.35, 0.0) * uBloom * 1.6;
            }

            col *= mix(1.0, 1.0 - 0.85 * smoothstep(0.3, 1.8, dot(q, q)), uVignette);

            if (uNoise > 0.0) {
                float n = hash12(floor(px) + vec2(uFrameRand * 1931.0, uFrameRand * 719.0)) - 0.5;
                col += n * uNoise * 0.22 * (0.4 + 0.6 * dot(col, kLuma));
                col += n * uNoise * 0.03;
            }
            if (uFlicker > 0.0)
                col *= 1.0 - uFlicker * (0.035 * (0.5 + 0.5 * sin(uTime * 6.2831 * 7.5)) + 0.025 * uFrameRand);
            // ---- film projection: grain, flicker, dust and scratches (24 film frames per second)
            if (uFilmGrain > 0.0) {
                float g = hash12(floor(px * 0.8) + vec2(filmFrame * 37.1, filmFrame * 11.7)) - 0.5;
                float g2 = hash12(floor(px * 0.4) + vec2(filmFrame * 13.3, filmFrame * 5.1)) - 0.5;
                float lum = dot(col, kLuma);
                col += (g * 0.6 + g2 * 0.4) * uFilmGrain * 0.22 * (0.2 + 3.2 * lum * (1.0 - lum));
            }
            if (uFilmFlicker > 0.0) col *= 1.0 + (hash12(vec2(filmFrame, 7.7)) - 0.5) * 0.12 * uFilmFlicker;
            if (uFilmDamage > 0.0) {
                // dust: a few specks, each for a single frame (dark on the print, bright on the negative)
                vec2 grid = vec2(28.0, 16.0);
                vec2 cell = floor(tuv * grid);
                if (hash12(cell + vec2(filmFrame * 3.1, filmFrame * 1.7)) > 1.0 - 0.014 * uFilmDamage) {
                    vec2 cp = (cell + vec2(hash12(cell + filmFrame), hash12(cell - filmFrame))) / grid;
                    float d = length((tuv - cp) * uTube.zw);
                    float r = 1.0 + 3.0 * hash12(cell * 1.7 + filmFrame);
                    float speck = 1.0 - smoothstep(r * 0.4, r, d);
                    col = mix(col, hash12(cell + 9.0 + filmFrame) > 0.3 ? vec3(0.03) : vec3(0.92), speck * 0.85);
                }
                // scratches: a thin vertical line that stays for a few frames, wavering
                float sFrame = floor(uTime * 24.0 / 7.0);
                float on = step(1.0 - 0.7 * uFilmDamage, hash12(vec2(sFrame, 8.1)));
                float sx = 0.1 + 0.8 * hash12(vec2(sFrame, 3.3));
                float dx = abs(tuv.x - sx - sin(tuv.y * 9.0 + sFrame) * 0.002) * uTube.z;
                col = mix(col, vec3(0.80), on * (1.0 - smoothstep(0.3, 1.1, dx)) * 0.45);
            }
            if (uTransType > 0) col = mix(col * transBoost, vec3(1.0), transWhite) * transMask * transLevel;
            col = clamp(col, 0.0, 1.0) * tubeMask;
        }
    }

    if (uSplit >= 0.0 && abs(px.x - uSplit) < 1.0)
        col = mix(col, vec3(0.95, 0.72, 0.36), 0.9);

    fragColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
