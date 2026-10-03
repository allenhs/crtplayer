#version 330 core
// FMV console, pass 2: the decoded picture shown with the frame's palette (a few dozen of
// the console's 512 colours), with ordered dithering aligned to the screen grid.
out vec4 fragColor;
uniform sampler2D uSrc;       // decoded picture (screen grid)
uniform sampler2D uPalette;   // uColors x 1
uniform int   uColors;
uniform float uDither;        // 0..1
uniform ivec2 uGrid;

float bayer4(ivec2 p)
{
    const float m[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);
    return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0;
}

void main()
{
    ivec2 g = ivec2(gl_FragCoord.xy);
    vec3 c = texelFetch(uSrc, g, 0).rgb;
    // About one step of the console's 3-bit grid, around the true colour.
    c = clamp(c + (bayer4(g) - 0.5) * uDither * 0.20, 0.0, 1.0);
    vec3 best = vec3(0.0);
    float bestD = 1e9;
    for (int i = 0; i < uColors; ++i) {
        vec3 p = texelFetch(uPalette, ivec2(i, 0), 0).rgb;
        vec3 d = p - c;
        float dist = dot(d * d, vec3(3.0, 4.0, 2.0));
        if (dist < bestD) { bestD = dist; best = p; }
    }
    fragColor = vec4(best, 1.0);
}
