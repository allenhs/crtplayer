#version 330 core
// Enhance: upscaling. A Lanczos-2 reconstruction of the picture (16 taps) at the size it is
// shown, with its overshoot held within what the four nearest source pixels show, so that
// edges come out sharp without the bright and dark halos a plain Lanczos filter leaves.
// (In the manner of the spatial upscalers games use; an independent implementation.)
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uSrcSize;     // texels
uniform vec2 uOutSize;
uniform float uAntiRing;   // 0 (plain Lanczos) .. 1 (no overshoot at all)

float lanczos2(float x)
{
    x = abs(x);
    if (x < 1e-4) return 1.0;
    if (x >= 2.0) return 0.0;
    float px = 3.14159265 * x;
    return sin(px) * sin(px * 0.5) / (px * px * 0.5);
}

void main()
{
    vec2 pos = gl_FragCoord.xy / uOutSize * uSrcSize - 0.5;   // in source texels
    vec2 f = fract(pos);
    ivec2 base = ivec2(floor(pos));
    ivec2 hi = ivec2(uSrcSize) - 1;
    vec3 sum = vec3(0.0);
    float wsum = 0.0;
    vec3 mn = vec3(1e9), mx = vec3(-1e9);
    for (int j = -1; j <= 2; ++j) {
        float wy = lanczos2(float(j) - f.y);
        for (int i = -1; i <= 2; ++i) {
            float w = lanczos2(float(i) - f.x) * wy;
            vec3 c = texelFetch(uSrc, clamp(base + ivec2(i, j), ivec2(0), hi), 0).rgb;
            sum += c * w;
            wsum += w;
            if (i >= 0 && i <= 1 && j >= 0 && j <= 1) { mn = min(mn, c); mx = max(mx, c); }
        }
    }
    vec3 c = sum / wsum;
    c = mix(c, clamp(c, mn, mx), uAntiRing);
    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0);
}
