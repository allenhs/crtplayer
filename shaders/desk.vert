#version 330 core
// Desk mode: procedural CRT cabinet. Material ids: 0 body, 1 front bezel, 2 screen
// tunnel, 3 glass (bulged here from the current curvature), 4 floor shadow.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aMat;

uniform mat4 uModel;
uniform mat4 uShadowModel;
uniform mat4 uVP;
uniform float uAspect;
uniform float uBulge;
uniform vec4 uShadowRect;   // min x, min z, max x, max z (rolled space)
uniform float uFloorY;

out vec3 vWorld;
out vec3 vNormal;
out vec3 vModel;
out vec3 vModelN;
flat out int vMat;

void main()
{
    int mat = int(aMat + 0.5);
    vec3 p = aPos;
    vec3 n = aNormal;
    mat4 M = uModel;
    if (mat == 3) {
        // Bulge that is zero on the whole rim, so the glass meets the tunnel exactly.
        float ax = 2.0 * p.x / uAspect, ay = 2.0 * p.y;
        p.z += uBulge * (1.0 - ax * ax) * (1.0 - ay * ay);
        float dzdx = uBulge * (-8.0 * p.x / (uAspect * uAspect)) * (1.0 - ay * ay);
        float dzdy = uBulge * (1.0 - ax * ax) * (-8.0 * p.y);
        n = normalize(vec3(-dzdx, -dzdy, 1.0));
    } else if (mat == 4) {
        p = vec3(mix(uShadowRect.x, uShadowRect.z, aPos.x), uFloorY, mix(uShadowRect.y, uShadowRect.w, aPos.z));
        M = uShadowModel;
    }
    vec4 w = M * vec4(p, 1.0);
    vWorld = w.xyz;
    vNormal = mat3(M) * n;
    vModelN = n;
    vModel = p;
    vMat = mat;
    gl_Position = uVP * w;
}
