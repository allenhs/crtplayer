#version 330 core
// Enhance: sharpening after upscaling. Contrast-adaptive: each pixel is sharpened by as much as
// its neighbourhood leaves room for before it would clip, so flat areas and noise are left
// alone and strong edges are not overdone. (The method of AMD's CAS; an independent implementation.)
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uSize;
uniform float uSharp;   // 0..1

vec3 px(ivec2 p) { return texelFetch(uSrc, clamp(p, ivec2(0), ivec2(uSize) - 1), 0).rgb; }

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec3 a = px(p + ivec2(-1, -1)), b = px(p + ivec2(0, -1)), c = px(p + ivec2(1, -1));
    vec3 d = px(p + ivec2(-1, 0)),  e = px(p),                f = px(p + ivec2(1, 0));
    vec3 g = px(p + ivec2(-1, 1)),  h = px(p + ivec2(0, 1)),  i = px(p + ivec2(1, 1));
    vec3 mn = min(min(min(d, e), min(f, b)), h);
    vec3 mx = max(max(max(d, e), max(f, b)), h);
    mn += min(mn, min(min(a, c), min(g, i)));
    mx += max(mx, max(max(a, c), max(g, i)));
    vec3 amp = sqrt(clamp(min(mn, 2.0 - mx) / max(mx, vec3(1e-4)), 0.0, 1.0));
    vec3 w = amp * (-1.0 / mix(8.0, 5.0, uSharp));
    vec3 o = (b * w + d * w + f * w + h * w + e) / (1.0 + 4.0 * w);
    fragColor = vec4(clamp(mix(e, o, step(0.001, uSharp)), 0.0, 1.0), 1.0);
}
