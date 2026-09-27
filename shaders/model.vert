#version 330 core
// Your 3D models in the 90s CG room. Floor frame: y up from the floor.
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNrm;
layout(location = 2) in vec3 aCol;
uniform mat4 uPlace;        // model -> floor frame (slot, turn, plinth height, scale)
uniform mat4 uFloorToWorld;
uniform mat4 uVP;
uniform int uMode;          // 0 the model, 1 its mirror image in the floor, 2 its shadow on the floor
uniform vec3 uSunF;         // sun direction, floor frame
out vec3 vPos;              // floor frame
out vec3 vNrm;
out vec3 vCol;
out vec3 vLocal;
void main()
{
    vec3 p = (uPlace * vec4(aPos, 1.0)).xyz;
    vec3 n = normalize(mat3(uPlace) * aNrm);
    if (uMode == 1) { p.y = -p.y; n.y = -n.y; }                          // mirrored in the floor
    if (uMode == 2) { p -= uSunF * (p.y / max(uSunF.y, 0.05)); p.y = 0.002; }   // flattened along the sun
    vPos = p; vNrm = n; vCol = aCol; vLocal = aPos;
    gl_Position = uVP * uFloorToWorld * vec4(p, 1.0);
}
