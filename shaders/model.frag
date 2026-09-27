#version 330 core
// Finishes for your 3D models: marble, bronze, chrome, candy plastic, or their own colours.
in vec3 vPos;
in vec3 vNrm;
in vec3 vCol;
in vec3 vLocal;
out vec4 fragColor;
uniform int uMode;          // 0 model, 1 mirror image (faint, onto the floor only), 2 shadow
uniform int uFinish;        // 0 marble, 1 bronze, 2 chrome, 3 plastic, 4 own colours; 5 plinth (marble)
uniform vec3 uTint;         // plastic colour
uniform vec3 uCamF;         // camera, floor frame
uniform vec3 uSunF;
uniform vec3 uSunColour;
uniform vec3 uAmbient;
uniform vec3 uSkyTop;
uniform vec3 uSkyHorizon;
uniform vec3 uTvF;          // the TV's screen centre, floor frame
uniform sampler2D uGlassTex;
uniform bool uHasGlass;
uniform float uGlassLod;

float hash2(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise2(vec2 p)
{
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1, 0)), u.x), mix(hash2(i + vec2(0, 1)), hash2(i + vec2(1, 1)), u.x), u.y);
}
vec3 env(vec3 d)   // what a chrome surface sees: the palette's sky above, a grey mirror floor below
{
    if (d.y > 0.0) return mix(uSkyHorizon, uSkyTop, pow(d.y, 0.55)) + uSunColour * pow(max(dot(d, uSunF), 0.0), 300.0) * 3.0;
    vec2 q = vec2(0.0);
    return mix(vec3(0.35), vec3(0.08), 0.5 + 0.5 * sign(sin(d.x / max(-d.y, 0.05) * 3.14) * sin(d.z / max(-d.y, 0.05) * 3.14)));
}
void main()
{
    if (uMode == 2) { fragColor = vec4(0.0, 0.0, 0.0, 0.5); return; }   // shadow: darkens the floor
    vec3 n = normalize(vNrm);
    vec3 V = normalize(uCamF - vPos);
    if (dot(n, V) < 0.0) n = -n;
    vec3 L = normalize(uSunF);
    float diff = max(dot(n, L), 0.0);
    vec3 H = normalize(L + V);
    vec3 tvL = uTvF - vPos;
    float tvD2 = dot(tvL, tvL);
    vec3 tv = (uHasGlass ? textureLod(uGlassTex, vec2(0.5), uGlassLod).rgb : vec3(0.0)) * max(dot(n, normalize(tvL)), 0.0) * max(-normalize(tvL).z, 0.0) / (tvD2 + 0.3);
    vec3 col;
    if (uFinish == 0 || uFinish == 5) {
        float v = sin(vLocal.x * 7.0 + vLocal.y * 4.0 + noise2(vLocal.xy * 4.0 + vLocal.z) * 5.0);
        vec3 alb = mix(vec3(0.93, 0.91, 0.87), vec3(0.50, 0.47, 0.48), pow(1.0 - abs(v), 7.0));
        col = alb * (uAmbient + uSunColour * diff + tv) + uSunColour * pow(max(dot(n, H), 0.0), 50.0) * 0.3;
    } else if (uFinish == 1) {   // bronze: warm metal, a little patina in the hollows
        vec3 alb = mix(vec3(0.62, 0.40, 0.18), vec3(0.25, 0.45, 0.38), smoothstep(0.55, 0.8, noise2(vLocal.xy * 6.0)) * 0.5);
        col = alb * (uAmbient * 0.8 + uSunColour * diff * 0.8 + tv) + alb * uSunColour * pow(max(dot(n, H), 0.0), 40.0) * 1.2
            + env(reflect(-V, n)) * alb * 0.25;
    } else if (uFinish == 2) {   // chrome
        col = env(reflect(-V, n)) * vec3(0.86, 0.87, 0.9) + uSunColour * pow(max(dot(n, H), 0.0), 300.0);
    } else if (uFinish == 3) {   // candy plastic
        col = uTint * (uAmbient + uSunColour * diff + tv) + uSunColour * pow(max(dot(n, H), 0.0), 120.0) * 0.9
            + uTint * pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.3;
    } else {                     // the model's own colours
        col = vCol * (uAmbient + uSunColour * diff + tv) + uSunColour * pow(max(dot(n, H), 0.0), 60.0) * 0.2;
    }
    col = clamp(col, 0.0, 1.0);
    if (uMode == 1) fragColor = vec4(col * 0.16, 0.0);   // mirror image: added faintly, floor pixels only
    else fragColor = vec4(col, 1.0);
}
