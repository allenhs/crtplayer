#version 330 core
// Bloom/glow pass B: separable 9-tap Gaussian (run horizontally, then vertically).
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uOutSize;
uniform vec2 uDir;      // (1,0) or (0,1), in texels
void main()
{
    vec2 uv = gl_FragCoord.xy / uOutSize;
    vec2 d = uDir / uOutSize;
    // Linear-sampling weights (5 fetches ~ 9 taps), sigma ~ 2.5 texels
    vec3 c = texture(uSrc, uv).rgb * 0.2270270270;
    c += texture(uSrc, uv + d * 1.3846153846).rgb * 0.3162162162;
    c += texture(uSrc, uv - d * 1.3846153846).rgb * 0.3162162162;
    c += texture(uSrc, uv + d * 3.2307692308).rgb * 0.0702702703;
    c += texture(uSrc, uv - d * 3.2307692308).rgb * 0.0702702703;
    fragColor = vec4(c, 1.0);
}
