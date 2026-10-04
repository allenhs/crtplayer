#version 330 core
// Frame generation: the picture at a moment between two frames. Every pixel is fetched
// from both frames along its motion (from where it was in the frame before, and where it
// will be in the frame after) and the two are mixed by how far along the moment is.
// Three motions are tried for each pixel (the one found from the frame before, the one
// found from the frame after, and none) and the one whose two fetches agree best is used.
// Where none agree (something uncovered, or motion that was not found) nothing is
// invented: the nearer frame is shown there as it is. The same across the whole picture
// when the two frames have nothing to do with each other (a cut).
out vec4 fragColor;
uniform sampler2D uA, uB;        // the frame before and the frame after (mipmapped)
uniform sampler2D uFwd, uBwd;    // motion A->B on A's grid, B->A on B's grid (.b: how well it matched)
uniform vec2 uOutSize;
uniform float uT;                // 0 = the frame before, 1 = the frame after
uniform int uMode;               // 0 normal; 1 plain mix of the two frames (for comparison)
uniform int uFine;               // 1: also try the surrounding motion vectors one by one (sharper edges of moving things)

void candidate(vec2 p, vec2 v, out vec3 a, out vec3 b, out float e)
{
    vec2 pa = p - uT * v, pb = p + (1.0 - uT) * v;
    a = textureLod(uA, pa, 0.0).rgb;
    b = textureLod(uB, pb, 0.0).rgb;
    vec3 d = abs(textureLod(uA, pa, 1.0).rgb - textureLod(uB, pb, 1.0).rgb);
    e = dot(d, vec3(0.299, 0.587, 0.114));
    vec2 oa = max(-pa, pa - 1.0), ob = max(-pb, pb - 1.0);
    if (max(max(oa.x, oa.y), max(ob.x, ob.y)) > 0.0) e += 0.25;   // (from outside the picture)
}

void main()
{
    vec2 p = gl_FragCoord.xy / uOutSize;
    vec3 fa = textureLod(uA, p, 0.0).rgb, fb = textureLod(uB, p, 0.0).rgb;
    if (uMode == 1) { fragColor = vec4(mix(fa, fb, uT), 1.0); return; }
    if (uMode == 2) { vec4 f = textureLod(uFwd, p, 0.0); fragColor = vec4(f.xy * 16.0 + 0.5, f.z * 4.0, 1.0); return; }
    if (uMode == 3) { vec4 f = textureLod(uBwd, p, 0.0); fragColor = vec4(-f.xy * 16.0 + 0.5, f.z * 4.0, 1.0); return; }
    vec3 nearest = uT < 0.5 ? fa : fb;
    // The motion of what is at p at this moment: read at p, then again where that says it came from.
    vec2 vf = textureLod(uFwd, p, 0.0).xy;
    vf = textureLod(uFwd, p - uT * vf, 0.0).xy;
    vec2 vb = -textureLod(uBwd, p, 0.0).xy;
    vb = -textureLod(uBwd, p + (1.0 - uT) * vb, 0.0).xy;
    vec3 a1, b1, a2, b2, a0, b0;
    float e1, e2, e0;
    candidate(p, vf, a1, b1, e1);
    candidate(p, vb, a2, b2, e2);
    candidate(p, vec2(0.0), a0, b0, e0);
    vec3 a = a1, b = b1;
    float e = e1;
    if (e2 < e) { a = a2; b = b2; e = e2; }
    // At the edge of something that moves, the vector read between two texels is a mix of two
    // motions and right for neither. The four texels around are tried each on its own.
    vec2 fs = vec2(textureSize(uFwd, 0));
    vec2 qf = (p - uT * vf) * fs - 0.5, qb = (p + (1.0 - uT) * vb) * fs - 0.5;
    ivec2 hi = ivec2(fs) - 1;
    for (int j = 0; j <= uFine; ++j)
        for (int i = 0; i <= uFine; ++i) {
            if (uFine == 0) break;
            vec3 ca, cb; float ce;
            vec2 v = texelFetch(uFwd, clamp(ivec2(floor(qf)) + ivec2(i, j), ivec2(0), hi), 0).xy;
            candidate(p, v, ca, cb, ce);
            if (ce < e) { a = ca; b = cb; e = ce; }
            v = -texelFetch(uBwd, clamp(ivec2(floor(qb)) + ivec2(i, j), ivec2(0), hi), 0).xy;
            candidate(p, v, ca, cb, ce);
            if (ce < e) { a = ca; b = cb; e = ce; }
        }
    if (e0 < e - 0.012) { a = a0; b = b0; e = e0; }   // (standing still only where it is clearly the better match)
    vec3 col = mix(nearest, mix(a, b, uT), 1.0 - smoothstep(0.07, 0.18, e));
    // A cut: the frames do not match anywhere.
    float whole = max(textureLod(uFwd, vec2(0.5), 20.0).b, textureLod(uBwd, vec2(0.5), 20.0).b);
    // (Measured: 0.005 between frames that follow each other, 0.08 to 0.10 across a cut.)
    col = mix(col, nearest, smoothstep(0.035, 0.06, whole));
    fragColor = vec4(col, 1.0);
}
