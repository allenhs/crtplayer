#version 330 core
// Frame generation: the picture at the size motion is searched at.
out vec4 fragColor;
uniform sampler2D uSrc;    // the picture (mipmapped)
uniform vec2 uOutSize;
uniform float uLod;
void main()
{
    vec3 c = textureLod(uSrc, gl_FragCoord.xy / uOutSize, uLod).rgb;
    fragColor = vec4(c, 1.0);   // (in colour: two areas of the same brightness and different colour are told apart)
}
