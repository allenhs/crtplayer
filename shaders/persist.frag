#version 330 core
// Phosphor persistence: keep the brighter of the new frame and the decaying afterglow.
// P22 phosphors: red and green glow longer than blue, so the decay is per channel.
out vec4 fragColor;
uniform sampler2D uCurrent;
uniform sampler2D uHistory;
uniform vec2 uOutSize;
uniform vec3 uDecay;        // per-channel multiplier for the elapsed time
void main()
{
    vec2 uv = gl_FragCoord.xy / uOutSize;
    vec3 cur = texture(uCurrent, uv).rgb;
    vec3 prev = texture(uHistory, uv).rgb * uDecay;
    fragColor = vec4(max(cur, prev), 1.0);
}
