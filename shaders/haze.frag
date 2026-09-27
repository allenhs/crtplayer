#version 330 core
// Fog in front of the set (drawn after it): a soft glow around the screen in the picture's
// colour, and a faint veil over everything, so the set sits inside the haze.
out vec4 fragColor;
uniform vec2 uViewport;
uniform vec2 uScreenNdc;       // the screen's centre on screen
uniform vec2 uScreenRadius;    // its half size on screen (NDC)
uniform sampler2D uGlassTex;   // the picture (its smallest mip level is the average colour)
uniform bool uHasGlass;
uniform float uGlassLod;
uniform vec3 uVeil;            // fog colour of the room
uniform float uFog;
void main()
{
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    vec2 d = (ndc - uScreenNdc) / max(uScreenRadius, vec2(0.02));
    float r = length(d);
    vec3 uScreenColour = uHasGlass ? textureLod(uGlassTex, vec2(0.5), uGlassLod).rgb : vec3(0.0);
    float halo = uFog * 0.38 * exp(-max(r - 0.7, 0.0) * 1.4) * smoothstep(0.0, 1.0, r);   // around, not over, the picture
    float veilA = uFog * 0.10;
    fragColor = vec4(uScreenColour * halo + uVeil * veilA, veilA);   // premultiplied
}
