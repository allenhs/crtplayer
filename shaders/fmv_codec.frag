#version 330 core
// FMV console, pass 1: the video as a Cinepak-style codec delivers it.
//  - 4x4 blocks. A block with little detail keeps one colour and four brightness values
//    (one per 2x2 quarter); a detailed block keeps every pixel's brightness and one colour
//    per quarter.
//  - A block that hardly changed since it was last drawn is left as it was (unless this
//    is a key frame), which is what makes still areas freeze and shimmer.
// Output: the "decoded" picture on the console's screen grid, black outside the window.
out vec4 fragColor;
uniform sampler2D uLow;      // the video at the window's resolution
uniform sampler2D uPrev;     // the previous decoded picture (screen grid)
uniform ivec2 uGrid;         // screen grid size
uniform ivec2 uInner;        // window size
uniform ivec2 uOffset;       // window position in the grid (the picture's textures have row 0 at the top)
uniform float uBlocks;       // codec strength 0..1
uniform bool  uHavePrev;     // false on key frames

float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

void main()
{
    ivec2 g = ivec2(gl_FragCoord.xy);
    ivec2 v = g - uOffset;   // position in the video window; blocks start at its top-left corner
    if (v.x < 0 || v.y < 0 || v.x >= uInner.x || v.y >= uInner.y) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    vec3 c = texelFetch(uLow, v, 0).rgb;
    if (uBlocks <= 0.0) { fragColor = vec4(c, 1.0); return; }

    ivec2 b0 = (v / 4) * 4;
    vec3 mean = vec3(0.0), prevMean = vec3(0.0);
    vec3 q[4] = vec3[4](vec3(0.0), vec3(0.0), vec3(0.0), vec3(0.0));
    float lmin = 1.0, lmax = 0.0;
    for (int j = 0; j < 4; ++j) {
        for (int i = 0; i < 4; ++i) {
            ivec2 p = min(b0 + ivec2(i, j), uInner - 1);
            vec3 s = texelFetch(uLow, p, 0).rgb;
            mean += s;
            q[(j / 2) * 2 + i / 2] += s;
            float l = luma(s);
            lmin = min(lmin, l);
            lmax = max(lmax, l);
            prevMean += texelFetch(uPrev, p + uOffset, 0).rgb;
        }
    }
    mean /= 16.0;
    prevMean /= 16.0;

    // Unchanged enough: keep what is there.
    vec3 dm = abs(mean - prevMean);
    if (uHavePrev && max(dm.r, max(dm.g, dm.b)) < 0.055 * uBlocks) { fragColor = vec4(texelFetch(uPrev, g, 0).rgb, 1.0); return; }

    ivec2 r = v - b0;
    vec3 quarter = q[(r.y / 2) * 2 + r.x / 2] * 0.25;
    vec3 outc;
    if (lmax - lmin < 0.03 + 0.20 * uBlocks) outc = mean + (luma(quarter) - luma(mean));      // flat block
    else outc = quarter + (luma(c) - luma(quarter));                                          // detailed block
    fragColor = vec4(clamp(outc, 0.0, 1.0), 1.0);
}
