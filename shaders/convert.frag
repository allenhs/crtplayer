#version 330 core
// Pass 1: decoded frame (NV12 / I420 / RGBA / BGRA planes as uploaded by GStreamer)
// -> oriented RGB image. Applies the stream's YCbCr matrix and range, and the
// rotation / mirror metadata. Row 0 of the output texture is the TOP of the picture.
out vec4 fragColor;
uniform vec2 uOutSize;
uniform int uFormat;            // 0 RGBA, 1 BGRA, 2 NV12, 3 I420
uniform sampler2D uTex0;        // Y or packed RGB
uniform sampler2D uTex1;        // UV (NV12) or U (I420)
uniform sampler2D uTex2;        // V (I420)
uniform vec3 uOrient0;          // oriented uv -> storage uv (row 0)
uniform vec3 uOrient1;          // (row 1)
uniform vec3 uYuvOffset;        // subtract before scaling
uniform vec3 uYuvScale;         // range expansion
uniform mat3 uYuvToRgb;         // YCbCr (Cb/Cr centred) -> RGB

void main()
{
    vec2 uv = gl_FragCoord.xy / uOutSize;          // oriented picture uv, v = 0 at top
    vec3 p = vec3(uv, 1.0);
    vec2 suv = vec2(dot(uOrient0, p), dot(uOrient1, p));
    vec3 rgb;
    if (uFormat == 0) {
        rgb = texture(uTex0, suv).rgb;
    } else if (uFormat == 1) {
        rgb = texture(uTex0, suv).bgr;
    } else {
        float y = texture(uTex0, suv).r;
        vec2 c = (uFormat == 2) ? texture(uTex1, suv).rg
                                : vec2(texture(uTex1, suv).r, texture(uTex2, suv).r);
        vec3 yuv = (vec3(y, c) - uYuvOffset) * uYuvScale;
        rgb = uYuvToRgb * yuv;
    }
    fragColor = vec4(clamp(rgb, 0.0, 1.0), 1.0);
}
