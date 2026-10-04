#version 330 core
// The picture with no effects: placed in the viewport, black around it. A program of its
// own, because a software OpenGL renderer runs every branch of a big shader for every
// pixel: effects off must not pay for the effects.
// With OSD defined: the set's own overlay (Cable TV's channel number, banners, guide) on top.
out vec4 fragColor;
uniform sampler2D uImageFull;
uniform vec2 uViewport;
uniform vec4 uImg;   // visible picture rect (px, top-left origin)
uniform vec4 uSrc;   // uv window of the picture shown there
#ifdef OSD
uniform sampler2D uOsd;
uniform float uOsdAlpha;
uniform vec4 uOsdRect;
#endif

void main()
{
    vec2 p = vec2(gl_FragCoord.x, uViewport.y - gl_FragCoord.y);
    vec2 t = (p - uImg.xy) / uImg.zw;
    vec2 edge = min(p - uImg.xy, uImg.xy + uImg.zw - p);
    float cover = clamp(min(edge.x, edge.y) + 0.5, 0.0, 1.0);
    vec3 c = texture(uImageFull, uSrc.xy + clamp(t, 0.0, 1.0) * uSrc.zw).rgb * cover;
#ifdef OSD
    vec2 picSize = uImg.zw / uSrc.zw;
    vec2 picOrigin = uImg.xy - uSrc.xy * picSize;
    vec2 l = ((p - picOrigin) / picSize - uOsdRect.xy) / uOsdRect.zw;
    if (all(greaterThanEqual(l, vec2(0.0))) && all(lessThanEqual(l, vec2(1.0)))) {
        vec4 o = texture(uOsd, l);
        c = c * (1.0 - o.a * uOsdAlpha) + o.rgb * uOsdAlpha;
    }
#endif
    fragColor = vec4(c, 1.0);
}
