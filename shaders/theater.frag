#version 330 core
// Movie theater, behind the screen: stage, screen fabric around the picture, side walls
// with sconces, ceiling and the carpeted audience floor. Lit by the picture and by the
// house lights (which dim as the curtains open). Coordinates: "theater space" = floor
// coordinates of the screen: x across, y up from the screen's bottom edge, z towards the
// audience; the screen is x in [-A/2, A/2], y in [0, 1], z = 0.
out vec4 fragColor;
uniform mat4 uInvVP;
uniform vec2 uViewport;
uniform vec3 uCamPos;
uniform vec3 uFloorPoint;   // theater-space origin (world)
uniform vec3 uFloorX;
uniform vec3 uFloorNormal;
uniform vec3 uFloorZ;
uniform float uAspect;
uniform sampler2D uGlassTex;
uniform bool uHasGlass;
uniform float uGlassLod;
uniform float uHouse;       // house lights 0..1
uniform float uFog;
uniform int uFogSteps;
uniform float uTime;

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise2(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}
vec3 picAvg() { return uHasGlass ? textureLod(uGlassTex, vec2(0.5), uGlassLod).rgb : vec3(0.0); }

float openW() { return max(uAspect, 2.39) + 0.30; }   // the proscenium opening
const float kStageY = -0.30, kStageFront = 0.55, kAudY = -0.45, kBackZ = -0.25, kCeil = 1.9;
float wallX() { return openW() * 0.5 + 0.9; }

// light from the picture (a big area light at z = 0, facing +z)
vec3 screenLight(vec3 p, vec3 n)
{
    vec3 c = vec3(0.0, 0.5, 0.0);
    vec3 L = c - p;
    float d2 = dot(L, L);
    vec3 l = L * inversesqrt(d2);
    float emit = max(-l.z, 0.0);
    return picAvg() * emit * max(dot(l, n), 0.0) * uAspect * 1.3 / (d2 + 0.3);
}
vec3 house() { return vec3(1.0, 0.78, 0.55) * uHouse; }

vec3 shadeWall(vec3 p, vec3 n)
{
    // Side walls: dark fabric panels, a brass rail, sconces washing light upward.
    float seam = 1.0 - smoothstep(0.0, 0.01, abs(fract(p.z * 2.0) - 0.5) - 0.48);
    vec3 fabric = vec3(0.07, 0.10, 0.12) * (0.9 + 0.1 * noise2(p.zy * 40.0)) * (1.0 - 0.4 * seam);
    float rail = 1.0 - smoothstep(0.0, 0.012, abs(p.y - 0.05));
    vec3 alb = mix(fabric, vec3(0.55, 0.42, 0.20), rail);
    // sconces every 1.2 along the wall at y = 1.0
    float zc = (floor(p.z / 1.2) + 0.5) * 1.2;
    vec2 d = vec2(p.z - zc, p.y - 1.0);
    // mostly upward, with a soft spill below the lamp (no hard edge)
    float wash = exp(-d.x * d.x * 18.0) * exp(-max(d.y, 0.0) * 2.5) * exp(min(d.y, 0.0) * 7.0);
    float lamp = exp(-dot(d, d) * 2500.0);
    vec3 col = alb * (house() * (0.12 + 1.1 * wash) + screenLight(p, n) + vec3(0.006));
    col += vec3(1.0, 0.8, 0.5) * lamp * (0.15 + 1.4 * uHouse);
    return col;
}

void main()
{
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    vec4 a = uInvVP * vec4(ndc, -1.0, 1.0), b = uInvVP * vec4(ndc, 1.0, 1.0);
    vec3 rw = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 ro = vec3(dot(uCamPos - uFloorPoint, uFloorX), dot(uCamPos - uFloorPoint, uFloorNormal), dot(uCamPos - uFloorPoint, uFloorZ));
    vec3 rd = vec3(dot(rw, uFloorX), dot(rw, uFloorNormal), dot(rw, uFloorZ));

    float t = 1e9; int what = -1; vec3 n = vec3(0.0);
    // planes: 0 screen wall (z = 0), 1 back (z = kBackZ), 2/3 side walls, 4 ceiling, 5 stage floor, 6 audience floor, 7 stage front
    float tt;
    if (rd.z < 0.0) { tt = -ro.z / rd.z; vec3 p = ro + rd * tt; if (tt > 0.0 && tt < t) { t = tt; what = 0; n = vec3(0, 0, 1); } }
    if (rd.x < 0.0) { tt = (-wallX() - ro.x) / rd.x; if (tt > 0.0 && tt < t) { t = tt; what = 2; n = vec3(1, 0, 0); } }
    if (rd.x > 0.0) { tt = (wallX() - ro.x) / rd.x; if (tt > 0.0 && tt < t) { t = tt; what = 3; n = vec3(-1, 0, 0); } }
    if (rd.y > 0.0) { tt = (kCeil - ro.y) / rd.y; if (tt > 0.0 && tt < t) { t = tt; what = 4; n = vec3(0, -1, 0); } }
    if (rd.y < 0.0) {
        tt = (kStageY - ro.y) / rd.y; vec3 p = ro + rd * tt;
        if (tt > 0.0 && tt < t && p.z < kStageFront && p.z > 0.0) { t = tt; what = 5; n = vec3(0, 1, 0); }
        tt = (kAudY - ro.y) / rd.y; p = ro + rd * tt;
        if (tt > 0.0 && tt < t && p.z >= kStageFront) { t = tt; what = 6; n = vec3(0, 1, 0); }
    }
    if (rd.z < 0.0) {
        tt = (kStageFront - ro.z) / rd.z; vec3 p = ro + rd * tt;
        if (tt > 0.0 && tt < t && p.y < kStageY && p.y > kAudY) { t = tt; what = 7; n = vec3(0, 0, 1); }
    }
    vec3 p = ro + rd * t;
    vec3 col = vec3(0.004);
    if (what == 0) {
        // the screen fabric around the picture (the masking and proscenium are drawn in front)
        vec3 fabric = vec3(0.62);
        vec3 bounce = picAvg() * 0.35 * exp(-length(max(abs(p.xy - vec2(0.0, 0.5)) - vec2(uAspect * 0.5, 0.5), 0.0)) * 3.0);
        col = fabric * (house() * 0.25 + bounce + vec3(0.004));
    } else if (what == 2 || what == 3) {
        col = shadeWall(p, n);
    } else if (what == 4) {
        col = vec3(0.03, 0.03, 0.035) * (house() * 0.5 + screenLight(p, n) + vec3(0.01));
    } else if (what == 5) {
        float plank = 1.0 - smoothstep(0.0, 0.01, abs(fract(p.x * 6.0) - 0.5) - 0.47);
        vec3 wood = vec3(0.10, 0.06, 0.035) * (1.0 - 0.3 * plank);
        col = wood * (house() * 0.4 + screenLight(p, n) + vec3(0.004));
    } else if (what == 6) {
        // carpet: deep red with a small repeating pattern
        vec2 c = fract(p.xz * 5.0) - 0.5;
        float dot_ = 1.0 - smoothstep(0.12, 0.18, length(c));
        vec3 carpet = mix(vec3(0.10, 0.02, 0.03), vec3(0.16, 0.10, 0.04), dot_ * 0.6);
        col = carpet * (house() * 0.35 + screenLight(p, n) + vec3(0.004));
    } else if (what == 7) {
        col = vec3(0.05, 0.02, 0.02) * (house() * 0.4 + screenLight(p, n) + vec3(0.004));
    }

    // haze: the projector's light scatters in it (the beam itself is drawn in front)
    if (uFog > 0.0) {
        float T = 1.0; vec3 ins = vec3(0.0);
        float tEnd = min(t, 12.0), dt = tEnd / float(uFogSteps);
        for (int i = 0; i < 64; ++i) {
            if (i >= uFogSteps) break;
            float s = (float(i) + hash2(gl_FragCoord.xy + float(i))) * dt;
            vec3 q = ro + rd * s;
            float dens = uFog * 0.35 * (0.6 + 0.4 * noise2(q.xz * 0.8 + uTime * 0.02)) * dt;
            vec3 lit = screenLight(q, normalize(vec3(0.0, 0.5, 0.0) - q)) * 0.6 + house() * 0.05;
            ins += T * dens * lit;
            T *= exp(-dens * 1.2);
        }
        col = col * T + ins;
    }
    fragColor = vec4(col, 1.0);
}
