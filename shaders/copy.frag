#version 330 core
out vec4 fragColor;
uniform sampler2D uSrc;
uniform vec2 uOutSize;
void main() { fragColor = vec4(texture(uSrc, gl_FragCoord.xy / uOutSize).rgb, 1.0); }
