#version 330 core
// Frame generation: where each part of frame A is found in frame B. Worked out coarse to
// fine on smaller copies of the two frames: the coarsest level searches widely,
// every finer level starts from the level before (its own vector and its neighbours', so
// that a moving edge can take the motion of the side it belongs to) and corrects it.
// Output: the vector (in fractions of the picture) and how well it matched (0 = exactly).
out vec4 fragColor;
uniform sampler2D uA, uB;     // the two frames, small and mipmapped
uniform sampler2D uPrev;      // the coarser level's result
uniform vec2 uSize;           // this level's size in texels
uniform float uLevel;
uniform int uHavePrev, uRadius, uPreds;
uniform float uStep;          // spacing of the candidates, in texels of this level
uniform int uSubTexel;        // the finest level: place the vector between texels

float matchCost(vec2 uv, vec2 v)
{
    vec2 t = 1.0 / uSize;
    float s = 0.0;
    for (int j = -1; j <= 1; ++j)
        for (int i = -1; i <= 1; ++i) {
            vec2 o = vec2(float(i), float(j)) * 1.5 * t;
            s += dot(abs(textureLod(uA, uv + o, uLevel).rgb - textureLod(uB, uv + o + v, uLevel).rgb), vec3(0.3, 0.5, 0.2));
        }
    return s / 9.0;
}

void main()
{
    vec2 uv = gl_FragCoord.xy / uSize;
    vec2 t = 1.0 / uSize;
    vec2 pred[5];
    pred[0] = vec2(0.0);
    int preds = 1;
    if (uHavePrev == 1) {
        vec2 d = 2.0 * t;   // one texel of the coarser level
        pred[0] = textureLod(uPrev, uv, 0.0).xy;
        pred[1] = textureLod(uPrev, uv + vec2(d.x, 0.0), 0.0).xy;
        pred[2] = textureLod(uPrev, uv - vec2(d.x, 0.0), 0.0).xy;
        pred[3] = textureLod(uPrev, uv + vec2(0.0, d.y), 0.0).xy;
        pred[4] = textureLod(uPrev, uv - vec2(0.0, d.y), 0.0).xy;
        preds = uPreds;
    }
    vec2 centre = pred[0];
    // Standing still is always tried, and wins a tie: flat areas have no motion to find.
    vec2 best = vec2(0.0);
    float bestC = matchCost(uv, vec2(0.0)) - 0.002 + 0.0006 * length(centre * uSize);
    for (int k = 0; k < preds; ++k)
        for (int j = -uRadius; j <= uRadius; ++j)
            for (int i = -uRadius; i <= uRadius; ++i) {
                vec2 off = vec2(float(i), float(j)) * uStep;
                vec2 v = pred[k] + off * t;
                // (a small price for leaving the prediction: among equal matches the smoother field wins)
                float c = matchCost(uv, v) + 0.0012 * length(off) + 0.0006 * length((v - centre) * uSize);
                if (c < bestC) { bestC = c; best = v; }
            }
    float c0 = matchCost(uv, best);
    if (uSubTexel == 1) {
        // Between texels: the lowest point of a parabola through the match at the best whole
        // texel and its two neighbours, each way. (Searching at half texels instead would favour
        // whole ones: a picture fetched between its pixels is blurred, and matches a little worse.)
        float xm = matchCost(uv, best - vec2(t.x, 0.0)), xp = matchCost(uv, best + vec2(t.x, 0.0));
        float ym = matchCost(uv, best - vec2(0.0, t.y)), yp = matchCost(uv, best + vec2(0.0, t.y));
        float dx = xm - 2.0 * c0 + xp, dy = ym - 2.0 * c0 + yp;
        vec2 sub = vec2(dx > 1e-5 ? clamp(0.5 * (xm - xp) / dx, -0.5, 0.5) : 0.0,
                        dy > 1e-5 ? clamp(0.5 * (ym - yp) / dy, -0.5, 0.5) : 0.0);
        best += sub * t;
    }
    fragColor = vec4(best, clamp(c0, 0.0, 1.0), 1.0);
}
