#version 330 core
// Bloom/glow pass A: downsample the (mipmapped) oriented image to a small,
// resolution-independent working size so blur radii are relative to the picture.
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uOutSize;
void main()
{
    vec2 uv = gl_FragCoord.xy / uOutSize;
    vec2 t = 0.5 / uOutSize;
    vec3 c = texture(uSrc, uv + vec2(-t.x, -t.y)).rgb + texture(uSrc, uv + vec2(t.x, -t.y)).rgb
           + texture(uSrc, uv + vec2(-t.x,  t.y)).rgb + texture(uSrc, uv + vec2(t.x,  t.y)).rgb;
    fragColor = vec4(c * 0.25, 1.0);
}
