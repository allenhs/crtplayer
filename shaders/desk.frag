#version 330 core
// Desk mode shading. Output is premultiplied alpha: the window around the set is
// transparent, so the TV appears to stand on the desktop.
in vec3 vWorld;
in vec3 vNormal;
in vec3 vModel;
in vec3 vModelN;
flat in int vMat;
out vec4 fragColor;

uniform sampler2D uGlassTex;   // CRT output, glass-shaped, bottom-up
uniform sampler2D uBlurTex;    // blurred picture, top-down
uniform bool uHasGlass;
uniform bool uHasBlur;
uniform vec3 uCamPos;
uniform float uAspect;
uniform bool uPivot;
uniform vec4 uPicInGlass;      // picture rect inside the glass (glass uv, top-down)
uniform vec4 uMargins;         // side, top, bottom margins, glass corner radius
uniform vec4 uShadowFoot;      // centre x, centre z, half x, half z (rolled space)
uniform float uLed;
uniform int uCabinet;
uniform float uLightLevel;     // room lighting (1 = normal)
uniform float uReveal;         // 90s CG room: the picture resolving tile by tile (1 = all shown)          // 0 CRT TV, 1 flat-face CRT, 2 flat panel, 3 wood console, 4 PVM, 5 beige monitor

float hash1(float n) { return fract(sin(n) * 43758.5453); }
float noise1(float x) { float i = floor(x), f = fract(x); return mix(hash1(i), hash1(i + 1.0), f * f * (3.0 - 2.0 * f)); }
// Walnut veneer: fine, low-contrast grain lines running along the boards, a gentle
// figure, and pores along the grain. Boards run horizontally on the front, back and
// sides, front-to-back on the top and bottom, and vertically on the legs. Fine detail
// fades out when it gets smaller than a pixel, so it never shimmers.
vec3 wood(vec3 p, vec3 n, bool isLeg, float footprint)
{
    float across, along;
    if (isLeg)                { across = p.x * 0.8 + p.z * 0.6; along = p.y; }
    else if (abs(n.y) > 0.6)  { across = p.x; along = p.z; }
    else                      { across = p.y; along = abs(n.x) > 0.6 ? p.z : p.x; }
    float wander = (noise1(along * 1.1 + 3.7) - 0.5) * 0.05 + (noise1(along * 3.3 + 11.0) - 0.5) * 0.012;
    // Irregular spacing: the grain's phase drifts across the board (no corduroy look).
    float u = (across + wander) * 55.0 + noise1(across * 18.0 + 5.0) * 2.2;
    float vis = 1.0 - smoothstep(0.25, 0.8, footprint * 55.0);   // hide lines below a pixel
    float line = 1.0 - smoothstep(0.0, 0.12, abs(fract(u) - 0.5) - 0.38);   // thin dark line
    line *= 0.35 + 0.65 * noise1(floor(u) * 3.1 + 0.5);                     // lines of varying strength
    // Figure: broad, gently wavy light/dark bands, visible from a distance.
    float fig = noise1((across + wander * 3.0) * 4.5 + noise1(along * 0.6 + 2.0) * 2.2);
    fig = smoothstep(0.15, 0.85, fig);
    float streak = noise1((across + wander) * 26.0 + 9.0);                    // finer secondary streaks
    float pores = noise1(along * 180.0 + floor(u) * 17.0) * vis;
    vec3 dark = vec3(0.21, 0.105, 0.05), light = vec3(0.38, 0.21, 0.10);
    vec3 base = mix(dark, light, 0.25 + 0.55 * fig + 0.20 * streak);
    return base * (1.0 - 0.22 * line * vis - 0.07 * pores);
}
uniform float uTime;

// ---- the arcade cabinet (uCabinet 6) ------------------------------------------------------
uniform vec4 uArc;              // screen half width, half height (upright), inner half width, floor y
uniform int uArcStyle;          // 0 space, 1 sunset, 2 neon, 3 70s woodgrain
uniform sampler2D uMarqueeTex;  // the marquee's title (RGBA, top-down)
uniform bool uHasMarquee;

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise2(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}
float stars(vec2 q, float density, float fp)
{
    vec2 cell = floor(q);
    if (hash2(cell) > density) return 0.0;
    vec2 at = vec2(hash2(cell + 1.7), hash2(cell + 4.1)) * 0.7 + 0.15;
    float r = 0.06 + 0.10 * hash2(cell + 9.3);
    return (1.0 - smoothstep(r * 0.3, r + fp * 1.5, length(fract(q) - at))) * (0.5 + 0.5 * hash2(cell + 2.2));
}
vec3 arcAccent()
{
    return uArcStyle == 0 ? vec3(0.10, 0.55, 1.00) : uArcStyle == 1 ? vec3(1.00, 0.45, 0.08)
         : uArcStyle == 2 ? vec3(1.00, 0.10, 0.75) : vec3(0.95, 0.55, 0.10);
}
// Printed art: q is a position on the surface, h 0..1 from bottom to top of the area.
vec3 arcArt(vec2 q, float h, float fp, bool backlit)
{
    if (uArcStyle == 0) {                                   // space: stars, a ringed planet, a neon grid
        vec3 c = mix(vec3(0.015, 0.02, 0.08), vec3(0.16, 0.04, 0.24), smoothstep(0.1, 1.0, h));
        c += vec3(0.9, 0.92, 1.0) * stars(q * 22.0, 0.10, fp * 22.0);
        // a ringed planet in the upper part
        vec2 pc = vec2(q.x + 0.55, (h - 0.72) * 3.2);
        float pr = 0.26;
        vec2 rp = vec2(pc.x * 0.95 + pc.y * 0.30, (pc.y * 0.95 - pc.x * 0.30) * 3.4);   // tilted ring plane
        float ringR = length(rp);
        bool ringFront = rp.y < 0.0;
        if (length(pc) < pr) {
            float band = 0.8 + 0.2 * sin(pc.y * 40.0 + pc.x * 6.0);
            float shade = clamp(0.55 + 1.6 * dot(normalize(vec3(pc, sqrt(max(pr * pr - dot(pc, pc), 0.0)))), normalize(vec3(-0.6, 0.5, 0.6))), 0.1, 1.2);
            c = vec3(0.95, 0.55, 0.25) * band * shade;
        }
        if (ringR > pr * 1.25 && ringR < pr * 1.85 && (ringFront || length(pc) >= pr))
            c = mix(c, vec3(0.85, 0.75, 0.95), 0.75 * (0.6 + 0.4 * sin(ringR * 60.0)));
        float horizon = 0.30;
        if (h < horizon) {                                  // neon grid floor
            float t = (horizon - h) / horizon;
            float gy = abs(fract(1.0 / (t + 0.08) * 0.6) - 0.5);
            float gx = abs(fract(q.x * 5.0 / (t + 0.3)) - 0.5);
            float line = max(1.0 - smoothstep(0.0, 0.05 + fp * 8.0, 0.5 - gy), 1.0 - smoothstep(0.0, 0.05 + fp * 8.0, 0.5 - gx));
            c = mix(vec3(0.03, 0.0, 0.06), vec3(0.95, 0.15, 0.85), line * (0.4 + 0.6 * t));
        }
        return c;
    }
    if (uArcStyle == 1) {                                   // sunset: bands, a striped sun, mountains
        float horizon = 0.42;
        vec3 sky = mix(vec3(1.0, 0.55, 0.12), vec3(0.85, 0.15, 0.45), smoothstep(horizon, horizon + 0.25, h));
        sky = mix(sky, vec3(0.20, 0.05, 0.35), smoothstep(horizon + 0.25, 1.0, h));
        vec3 c = sky;
        vec2 sc = vec2(q.x * 0.9, h - horizon - 0.05);
        float sun = length(sc * vec2(1.0, 1.6)) - 0.18;
        if (sun < 0.0 && !(sc.y < 0.06 && fract(sc.y * 22.0) < 0.35)) c = mix(vec3(1.0, 0.85, 0.2), vec3(1.0, 0.35, 0.3), smoothstep(0.1, -0.1, sc.y));
        float ridge = horizon + 0.05 + 0.07 * noise2(vec2(q.x * 3.0, 1.0)) + 0.03 * noise2(vec2(q.x * 9.0, 4.0));
        if (h < ridge) c = vec3(0.10, 0.02, 0.14);
        if (h < horizon) {
            float t = (horizon - h) / horizon;
            float gy = abs(fract(1.0 / (t + 0.1) * 0.5) - 0.5);
            c = mix(c, vec3(1.0, 0.3, 0.6), (1.0 - smoothstep(0.0, 0.04 + fp * 8.0, 0.5 - gy)) * 0.8);
        }
        return c;
    }
    if (uArcStyle == 2) {                                   // neon: black, bright stripes and triangles
        vec3 c = vec3(0.02, 0.02, 0.03);
        float d = q.x * 0.7 + h * 1.4;
        float band = fract(d * 1.6);
        if (band < 0.10) c = vec3(1.0, 0.1, 0.75);
        else if (band > 0.16 && band < 0.20) c = vec3(0.1, 0.9, 1.0);
        vec2 tq = fract(q * 3.0) - 0.5;
        float tri = max(abs(tq.x) * 0.866 + tq.y * 0.5, -tq.y) - 0.18;
        if (abs(tri) < 0.015 + fp * 3.0 && hash2(floor(q * 3.0)) > 0.6) c = vec3(0.95, 0.95, 0.3);
        return c;
    }
    // 70s: wood grain with a bold three-colour stripe
    vec3 c = wood(vec3(q.x, q.y, 0.0), vec3(0.0, 0.0, 1.0), false, fp);
    float d = h * 1.0 - q.x * 0.35;
    float s = fract(d * 1.2);
    if (s < 0.07) c = vec3(0.95, 0.55, 0.08);
    else if (s < 0.14) c = vec3(0.90, 0.30, 0.05);
    else if (s < 0.21) c = vec3(0.55, 0.12, 0.03);
    return c;
}
vec3 buttonColour(int k)
{
    const vec3 cols[8] = vec3[](vec3(0.9, 0.08, 0.06), vec3(1.0, 0.8, 0.05), vec3(0.1, 0.35, 1.0), vec3(0.1, 0.8, 0.2),
                                vec3(0.92, 0.92, 0.9), vec3(1.0, 0.45, 0.05), vec3(0.95, 0.95, 0.95), vec3(0.95, 0.3, 0.7));
    return cols[k];
}
// The arcade cabinet's surfaces, in the upright frame.
void arcadeSurface(vec3 u, vec3 n, float fp, out vec3 alb, out vec3 emissive, out float gloss, out float specK)
{
    float uw = uArc.x, uh = uArc.y, ci = uArc.z, yf = uArc.w;
    float top = uh + 0.72, height = top - yf;
    emissive = vec3(0.0);
    alb = uArcStyle == 3 ? wood(u, n, false, fp) : vec3(0.025, 0.025, 0.03);   // black laminate (or wood)
    gloss = 30.0; specK = 0.12;
    if (vMat == 9) {                                        // side art
        alb = arcArt(vec2(u.z * 0.8, u.y) , (u.y - yf) / height, fp, false);
        gloss = 45.0; specK = 0.18;
    } else if (vMat == 15) {                                // T-molding
        alb = arcAccent() * (uArcStyle == 3 ? 0.08 : 0.9);
        gloss = 60.0; specK = 0.5;
    } else if (vMat == 10) {                                // backlit marquee
        float mu = u.x / ci * 0.5 + 0.5, mv = clamp((u.y - (uh + 0.25)) / 0.41, 0.0, 1.0);
        vec3 art = arcArt(vec2(u.x * 0.7, mv * 0.6), 0.3 + mv * 0.6, fp, true);
        vec4 txt = uHasMarquee ? texture(uMarqueeTex, vec2(mu, 1.0 - mv)) : vec4(0.0);
        vec3 lit = art * 0.85 * (1.0 - txt.a) + txt.rgb;
        float edge = smoothstep(0.0, 0.04, min(min(mu, 1.0 - mu) * 4.0, min(mv, 1.0 - mv)));
        emissive = lit * (0.55 + 0.45 * edge) * 1.25;      // a little darker at the edges of the lightbox
        alb = vec3(0.02); gloss = 90.0; specK = 0.25;
    } else if (vMat == 11) {                                // control panel overlay
        float s = clamp((0.97 - u.z) / 0.75, 0.0, 1.0);
        alb = arcArt(vec2(u.x * 0.8, s * 0.4), 0.15 + s * 0.35, fp, false) * 0.8;
        float border = min(ci - abs(u.x), min(s, 1.0 - s) * 0.75);
        if (border < 0.03) alb = arcAccent() * 0.8;
        gloss = 70.0; specK = 0.30;                         // plexiglass over the print
    } else if (vMat == 18) {                                // monitor bezel: black glass, a thin printed frame
        float d = max(abs(u.x) - uw, abs(u.y) - uh);
        alb = vec3(0.012, 0.012, 0.016);
        if (d > 0.035 && d < 0.045) alb = arcAccent() * 0.55;
        gloss = 120.0; specK = 0.45;
    } else if (vMat == 16) {                                // speaker panel
        alb = vec3(0.03);
        for (int k = -1; k <= 1; k += 2) {
            vec2 sp = vec2(u.x - float(k) * ci * 0.5, u.z - 0.19);
            if (length(sp * vec2(1.0, 1.6)) < 0.17) {
                vec2 h = fract(vec2(u.x, u.z) * 60.0) - 0.5;
                alb = mix(vec3(0.005), vec3(0.05), smoothstep(0.18, 0.28, length(h)));
            }
        }
    } else if (vMat == 14) {                                // coin door: brushed metal, slots, lit returns
        float br = noise2(vec2(u.x * 400.0, u.y * 4.0));
        alb = vec3(0.16, 0.16, 0.17) * (0.9 + 0.2 * br);
        gloss = 50.0; specK = 0.45;
        for (int k = -1; k <= 1; k += 2) {
            vec2 c = vec2(u.x - float(k) * 0.13, u.y - (yf + 1.14));
            if (abs(c.x) < 0.012 && abs(c.y) < 0.05) alb = vec3(0.005);                     // coin slot
            vec2 r = vec2(u.x - float(k) * 0.13, u.y - (yf + 0.98));
            if (abs(r.x) < 0.055 && abs(r.y) < 0.04) {                                      // lit coin return
                alb = vec3(0.3, 0.02, 0.01);
                emissive += vec3(1.0, 0.18, 0.04) * uLed * (0.8 + 0.4 * noise2(r * 60.0));
            }
        }
        vec2 lk = vec2(u.x, u.y - (yf + 0.76));
        if (length(lk) < 0.025) { alb = vec3(0.55, 0.55, 0.52); specK = 0.8; }               // lock
        if (abs(u.z - 0.07) > 0.005) alb *= 0.7;                                            // the door's edges
    } else if (vMat == 17) {                                // chrome
        alb = vec3(0.55, 0.56, 0.58); gloss = 110.0; specK = 0.9;
    } else if (vMat == 12) {                                // joystick ball
        alb = vec3(0.75, 0.04, 0.03); gloss = 90.0; specK = 0.7;
    } else if (vMat >= 20 && vMat <= 27) {                  // buttons: glossy, softly lit from inside
        vec3 bc = buttonColour(vMat - 20);
        alb = bc * 0.8; gloss = 90.0; specK = 0.55;
        emissive = bc * 0.18 * uLed;
    }
}

float sdRoundBox(vec2 p, vec2 b, float r)
{
    vec2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

// Display-oriented, top-down uv of a point on the glass (handles the pivoted set).
vec2 glassUV(vec2 m)
{
    return uPivot ? vec2(0.5 - m.y, 0.5 - m.x / uAspect) : vec2(m.x / uAspect + 0.5, 0.5 - m.y);
}

// Blurred picture colour at the glass point nearest to m: the light the screen casts.
vec3 screenLight(vec2 m)
{
    if (!uHasBlur) return vec3(0.0);
    vec2 c = clamp(m, -vec2(uAspect * 0.5, 0.5), vec2(uAspect * 0.5, 0.5));
    vec2 t = (glassUV(c) - uPicInGlass.xy) / uPicInGlass.zw;
    if (any(lessThan(t, vec2(0.0))) || any(greaterThan(t, vec2(1.0)))) return vec3(0.0);
    return texture(uBlurTex, t).rgb;
}

// A dim room with one soft window, for reflections on the glass.
vec3 room(vec3 r)
{
    float up = clamp(r.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 c = mix(vec3(0.012, 0.013, 0.016), vec3(0.07, 0.075, 0.085), up);
    float win = smoothstep(0.92, 0.985, dot(normalize(r), normalize(vec3(-0.45, 0.55, 0.7))));
    return c + vec3(0.5, 0.52, 0.56) * win;
}

void main()
{
    if (vMat == 4) {   // soft contact shadow on the floor plane
        // Seen from below the floor plane there is nothing to see: the shadow lies on the
        // floor's upper side. Fade out towards grazing angles so it never pops.
        float above = dot(normalize(vNormal), normalize(uCamPos - vWorld));
        if (above <= 0.0) discard;
        vec2 d = vec2(vModel.x - uShadowFoot.x, vModel.z - uShadowFoot.y);
        // Contact shadow: solid under the whole set (you can see under sets on legs or a
        // stand), a little deeper towards the middle, fading softly outwards. Raised sets
        // cast a softer edge.
        float s = sdRoundBox(d, uShadowFoot.zw - vec2(0.04), 0.12);
        bool raised = uCabinet == 2 || uCabinet == 3 || uCabinet == 5;
        float a;
        if (s > 0.0) a = 0.42 * exp(-s * (raised ? 4.5 : 7.0));
        else a = mix(0.42, 0.55, clamp(-s / 0.35, 0.0, 1.0));
        if (uCabinet == 3) {
            // Darker contact spots where the console's four legs meet the floor.
            vec2 q = abs(d) - (uShadowFoot.zw - vec2(0.10, 0.12));
            float spot = exp(-dot(q, q) / 0.0012);
            a = max(a, 0.75 * spot);
        }
        a *= smoothstep(0.0, 0.12, above);
        fragColor = vec4(0.0, 0.0, 0.0, a);
        return;
    }

    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCamPos - vWorld);
    if (dot(N, V) < 0.0) N = -N;
    vec3 L1 = normalize(vec3(-0.45, 0.75, 0.55));   // key light, upper left
    vec3 L2 = normalize(vec3(0.6, 0.15, 0.45));     // fill, right
    vec2 m = vModel.xy;
    float woodFootprint = length(fwidth(vModel));   // model units per pixel (uniform control flow)

    if (vMat == 3) {   // glass
        vec3 emis = vec3(0.0);
        if (uHasGlass) {
            vec2 g = glassUV(m);
            emis = texture(uGlassTex, vec2(g.x, 1.0 - g.y)).rgb;
            if (uReveal < 1.0) {
                // "rendering": tiles appear in a scattered order, like a ray tracer's buckets
                vec2 cell = floor(g * vec2(16.0, 9.0));
                float order = fract(sin(dot(cell, vec2(12.9898, 78.233))) * 43758.5453);
                if (order > uReveal) {
                    vec2 f = fract(g * vec2(16.0, 9.0));
                    float frame = step(min(min(f.x, 1.0 - f.x), min(f.y, 1.0 - f.y)), 0.04);
                    emis = vec3(0.02) + vec3(0.25, 0.3, 0.35) * frame * step(order, uReveal + 0.08);   // the bucket being rendered
                }
            }
        }
        vec3 R = reflect(-V, N);
        float F = 0.04 + 0.96 * pow(1.0 - max(dot(N, V), 0.0), 5.0);
        vec3 col;
        if (uCabinet == 7) {
            // Theater screen: matte projection fabric, only the projected light.
            col = emis + vec3(0.012);
        } else if (uCabinet == 2) {
            // Flat panel: deep LCD black, matte anti-glare coating: a soft sheen, no mirror.
            vec3 dark = vec3(0.016, 0.018, 0.024) * (0.4 + 0.4 * max(dot(N, L1), 0.0));
            col = emis + dark + room(R) * mix(0.03, 0.30, F) * 0.5;
            col += vec3(1.0) * pow(max(dot(R, L1), 0.0), 24.0) * 0.06;
        } else {
            vec3 dark = vec3(0.07, 0.08, 0.075) * (0.3 + 0.5 * max(dot(N, L1), 0.0));   // unlit phosphor
            col = emis + dark + room(R) * mix(0.18, 1.0, F);
            col += vec3(1.0) * pow(max(dot(R, L1), 0.0), 140.0) * 0.5;
        }
        if (sdRoundBox(m, vec2(uAspect * 0.5, 0.5), uMargins.w) > 0.0) col = vec3(0.02);
        fragColor = vec4(col, 1.0);
        return;
    }

    // Plastic: charcoal 90s consumer set, or glossy black for the flat panel.
    vec3 alb = (uCabinet == 2) ? vec3(0.03, 0.031, 0.035) : vec3(0.085, 0.087, 0.093);
    vec3 emissive = vec3(0.0);
    float gloss = 40.0, specK = 0.22;
    if (uCabinet == 2 && vMat != 3) { gloss = 90.0; specK = 0.35; }
    float by = -0.5 - uMargins.z * 0.5;                 // centre line of the lower panel
    if (uCabinet == 6) {                                  // arcade cabinet (drawn upright: undo a pivot)
        vec3 up = uPivot ? vec3(-vModel.y, vModel.x, vModel.z) : vModel;
        vec3 upn = uPivot ? vec3(-vModelN.y, vModelN.x, vModelN.z) : vModelN;
        arcadeSurface(up, upn, woodFootprint, alb, emissive, gloss, specK);
    } else if (vMat == 8) {                                      // wall mount: dark brushed metal
        alb = vec3(0.11, 0.11, 0.12); gloss = 60.0; specK = 0.45;
    } else if (vMat >= 5 && vMat <= 7) {                  // knobs
        // Which knob: two on the lower right of the console's front panel.
        vec2 c1 = vec2(uAspect * 0.5 + uMargins.x - 0.16, by + 0.09), c2 = vec2(c1.x, by - 0.09);
        bool upper = length(m - c1) < length(m - c2);
        vec2 d = m - (upper ? c1 : c2);
        float capR = (upper ? 0.055 : 0.045) * 0.80;
        if (vMat == 7) {                                  // metal flange
            alb = vec3(0.30, 0.29, 0.27); gloss = 90.0; specK = 0.7;
        } else if (vMat == 5) {                           // fluted bakelite body
            float ang = atan(d.y, d.x);
            float flute = 0.5 + 0.5 * cos(ang * 24.0);
            alb = vec3(0.045, 0.035, 0.03) * (0.75 + 0.5 * flute);
            gloss = 70.0; specK = 0.25 + 0.35 * flute;
        } else {                                          // brushed aluminium cap
            float r = length(d);
            float brushed = fract(sin(floor(r * 1500.0) * 12.9898) * 43758.5453);
            alb = vec3(0.66, 0.64, 0.60) * (0.90 + 0.10 * brushed);
            gloss = 45.0; specK = 0.8;
            if (r > capR * 0.88) alb *= 0.72;             // darker rim
            vec2 dir = upper ? normalize(vec2(-0.55, 0.83)) : normalize(vec2(0.70, 0.71));
            float along = dot(d, dir), perp = abs(d.x * dir.y - d.y * dir.x);
            if (along > capR * 0.12 && along < capR * 0.78 && perp < capR * 0.07) alb = vec3(0.07, 0.06, 0.05);   // pointer
        }
    } else if (uCabinet == 3) {                           // 80s wood-grain console
        bool isLeg = vModel.y < -0.5 - uMargins.z - 0.02;
        alb = wood(vModel, vModelN, isLeg, woodFootprint); gloss = 22.0; specK = 0.12;   // satin finish
        if (vMat == 1) {
            float gx0 = -uAspect * 0.5 - uMargins.x + 0.06, gx1 = uAspect * 0.5 + uMargins.x - 0.30;
            if (m.x > gx0 && m.x < gx1 && m.y < -0.5 - 0.06 && m.y > -0.5 - uMargins.z + 0.06) {
                // woven fabric speaker grille
                float wv = step(0.5, fract(m.x * 140.0)) * 0.5 + step(0.5, fract(m.y * 140.0)) * 0.5;
                alb = mix(vec3(0.10, 0.08, 0.06), vec3(0.16, 0.12, 0.09), wv); specK = 0.03;
            }
            if (m.y > -0.5 - 0.035 && m.y < -0.5 - 0.02) { alb = vec3(0.55, 0.52, 0.48); specK = 0.6; gloss = 80.0; }   // chrome trim
            vec2 led = vec2(uAspect * 0.5 + uMargins.x - 0.16, by - 0.19);
            float dl = length(m - led);
            emissive += vec3(1.0, 0.15, 0.05) * uLed * (smoothstep(0.010, 0.005, dl) * 1.5 + smoothstep(0.035, 0.0, dl) * 0.2);
        }
        if (vMat == 2) alb = vec3(0.05, 0.045, 0.04);
    } else if (uCabinet == 4) {                           // broadcast monitor: metal case, button strip
        alb = vMat == 1 ? vec3(0.10, 0.10, 0.11) : vec3(0.21, 0.22, 0.23);
        gloss = 30.0; specK = vMat == 1 ? 0.15 : 0.35;
        if (vMat == 1) {
            for (int i = 0; i < 8; ++i) {
                vec2 c = vec2(-uAspect * 0.5 + 0.06 + float(i) * 0.075, by);
                vec2 d = abs(m - c) - vec2(0.025, 0.018);
                if (max(d.x, d.y) < 0.0) alb = vec3(0.62, 0.62, 0.60);
            }
            vec2 led = vec2(uAspect * 0.5 - 0.02, by);
            float dl = length(m - led);
            emissive += vec3(0.2, 1.0, 0.3) * uLed * (smoothstep(0.010, 0.005, dl) * 1.4 + smoothstep(0.03, 0.0, dl) * 0.2);
        }
        if (vMat == 2) alb = vec3(0.04);
    } else if (uCabinet == 5) {                           // beige PC monitor
        alb = vMat == 2 ? vec3(0.12, 0.115, 0.10) : vec3(0.78, 0.74, 0.64);
        gloss = 25.0; specK = 0.10;
        if (vMat == 1) {
            vec2 led = vec2(uAspect * 0.5 - 0.02, by);
            float dl = length(m - led);
            emissive += vec3(0.25, 1.0, 0.3) * uLed * (smoothstep(0.009, 0.004, dl) * 1.4 + smoothstep(0.03, 0.0, dl) * 0.2);
            vec2 d = abs(m - vec2(uAspect * 0.5 - 0.10, by)) - vec2(0.03, 0.014);
            if (max(d.x, d.y) < 0.0) alb = vec3(0.66, 0.62, 0.53);   // power button
        }
    } else if (vMat == 1 && uCabinet == 2) {
        alb = vec3(0.022, 0.023, 0.026);
        vec2 led = vec2(0.0, -0.5 - uMargins.z * 0.5);          // small white LED, bottom centre
        float dl = length(m - led);
        emissive += vec3(0.85, 0.92, 1.0) * uLed * (smoothstep(0.006, 0.003, dl) * 1.4 + smoothstep(0.02, 0.0, dl) * 0.15);
    } else if (vMat == 1) {
        alb = vec3(0.105, 0.107, 0.114);
        float gx0 = -uAspect * 0.5, gx1 = -uAspect * 0.12;  // speaker grille, lower left
        if (m.x > gx0 && m.x < gx1 && abs(m.y - by) < 0.075)
            alb *= mix(1.0, 0.3, step(0.55, fract(m.y * 95.0)));
        for (int i = 0; i < 3; ++i) {                        // front buttons, lower right
            vec2 c = vec2(uAspect * 0.5 - 0.30 + float(i) * 0.075, by);
            float d = length(m - c);
            alb *= mix(1.0, 0.55, smoothstep(0.024, 0.02, d) * smoothstep(0.012, 0.016, d));
        }
        vec2 led = vec2(uAspect * 0.5 - 0.055, by);
        float dl = length(m - led);
        emissive += vec3(1.0, 0.12, 0.05) * uLed * (smoothstep(0.012, 0.006, dl) * 1.6 + smoothstep(0.04, 0.0, dl) * 0.25);
    } else if (vMat == 2 && uCabinet <= 2) {
        alb = (uCabinet == 2) ? vec3(0.015, 0.015, 0.018) : vec3(0.045, 0.045, 0.05);
    }

    vec3 H = normalize(L1 + V);
    float diff = max(dot(N, L1), 0.0) + max(dot(N, L2), 0.0) * 0.35;
    float spec = pow(max(dot(N, H), 0.0), gloss) * specK;
    vec3 rim = vec3(0.7, 0.8, 1.0) * pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.16;
    vec3 col = (alb * (0.35 + diff) + spec + rim) * uLightLevel + emissive;

    // The picture lights the tunnel and, faintly, the bezel around it.
    if (vModel.z > -0.2) {
        float dist = sdRoundBox(m, vec2(uAspect * 0.5, 0.5), uMargins.w);
        float fall = exp(-max(dist, 0.0) * 16.0);
        float k = (vMat == 2) ? 0.9 : ((vMat == 1 || vMat == 18) ? 0.35 : (vMat == 11 ? 0.25 : 0.0));
        if (uCabinet == 2) k *= 0.35;   // thin bezel: little area for light to spill onto
        col += screenLight(m) * fall * k * (alb * 4.0 + 0.12);
    }
    fragColor = vec4(col, 1.0);
}
