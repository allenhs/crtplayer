#version 330 core
// Lens flare for the 90s CG room: a glow at the sun and a chain of coloured ghosts along the
// line through the centre of the view. Additive; only when the sun is in view.
out vec4 fragColor;
uniform vec2 uViewport;
uniform vec2 uSunNdc;
uniform float uStrength;     // 0 = sun not in view
uniform vec3 uTint;
uniform bool uBanding;
void main()
{
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    float aspect = uViewport.x / uViewport.y;
    vec2 p = ndc * vec2(aspect, 1.0), s = uSunNdc * vec2(aspect, 1.0);
    vec3 c = uTint * 0.35 * exp(-length(p - s) * 6.0);                        // glow at the sun
    c += uTint * 0.25 * exp(-abs((p - s).y) * 60.0) * exp(-abs((p - s).x) * 1.5);   // horizontal streak
    const float ks[5] = float[](0.35, 0.6, 0.85, 1.25, 1.6);
    const float rs[5] = float[](0.05, 0.11, 0.035, 0.16, 0.08);
    const vec3 cs[5] = vec3[](vec3(1.0, 0.6, 0.3), vec3(0.4, 1.0, 0.6), vec3(0.6, 0.5, 1.0), vec3(1.0, 0.4, 0.8), vec3(0.4, 0.8, 1.0));
    for (int i = 0; i < 5; ++i) {
        vec2 g = s * (1.0 - 2.0 * ks[i]);                                      // along the line through the centre
        float d = length(p - g);
        c += cs[i] * 0.10 * (1.0 - smoothstep(rs[i] * 0.85, rs[i], d)) * (0.5 + 0.5 * smoothstep(0.0, rs[i], d));
    }
    c *= uStrength;
    if (uBanding) c = floor(c * 20.0 + 0.5) / 20.0;
    fragColor = vec4(c, 0.0);   // premultiplied: light only
}
