#version 330 core
// Room backdrop for desk mode where the desktop cannot show through (Steam Game Mode /
// gamescope, or a desktop without a compositor): a walnut desk surface on the set's floor
// plane, lit from the upper left and fading into the dark, and a dim wall behind it.
out vec4 fragColor;
uniform mat4 uInvVP;
uniform vec2 uViewport;
uniform vec3 uCamPos;
uniform vec3 uFloorPoint;
uniform vec3 uFloorNormal;
uniform vec3 uFloorX;        // board direction on the floor
uniform vec3 uFloorZ;
uniform vec2 uSetCentre;     // the set's footprint centre (floor coordinates)

float hash1(float n) { return fract(sin(n) * 43758.5453); }
float noise1(float x) { float i = floor(x), f = fract(x); return mix(hash1(i), hash1(i + 1.0), f * f * (3.0 - 2.0 * f)); }

vec3 deskWood(vec2 q, float footprint)
{
    // Boards along x; fine irregular grain; gentle figure (as on the console, a bit lighter).
    float wander = (noise1(q.x * 1.1 + 3.7) - 0.5) * 0.05;
    float u = (q.y + wander) * 40.0 + noise1(q.y * 14.0 + 5.0) * 2.0;
    float vis = 1.0 - smoothstep(0.25, 0.8, footprint * 40.0);
    float line = 1.0 - smoothstep(0.0, 0.12, abs(fract(u) - 0.5) - 0.38);
    line *= 0.35 + 0.65 * noise1(floor(u) * 3.1 + 0.5);
    float fig = smoothstep(0.15, 0.85, noise1((q.y + wander * 3.0) * 3.5 + noise1(q.x * 0.5 + 2.0) * 2.0));
    float seam = 1.0 - smoothstep(0.0, 0.004, abs(fract(q.y * 1.6) - 0.5) - 0.497);   // board seams
    vec3 dark = vec3(0.20, 0.11, 0.055), light = vec3(0.36, 0.21, 0.11);
    vec3 c = mix(dark, light, 0.3 + 0.6 * fig);
    return c * (1.0 - 0.18 * line * vis - 0.35 * seam * vis);
}

void main()
{
    vec2 ndc = gl_FragCoord.xy / uViewport * 2.0 - 1.0;
    vec4 a = uInvVP * vec4(ndc, -1.0, 1.0), b = uInvVP * vec4(ndc, 1.0, 1.0);
    vec3 ro = uCamPos, rd = normalize(b.xyz / b.w - a.xyz / a.w);
    float denom = dot(rd, uFloorNormal);
    float t = denom < -1e-4 ? dot(uFloorPoint - ro, uFloorNormal) / denom : -1.0;

    // Wall: dim, warm, with a soft pool of light behind the set.
    float h = clamp(ndc.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 wall = mix(vec3(0.045, 0.042, 0.050), vec3(0.11, 0.095, 0.09), h * 0.7);
    wall += vec3(0.12, 0.095, 0.07) * exp(-dot(ndc - vec2(0.0, 0.15), ndc - vec2(0.0, 0.15)) * 1.4);
    vec3 col = wall;
    if (t > 0.0) {
        vec3 hit = ro + rd * t;
        vec3 d = hit - uFloorPoint;
        vec2 q = vec2(dot(d, uFloorX), dot(d, uFloorZ));
        float footprint = length(fwidth(q));
        vec3 wood = deskWood(q, footprint);
        // Lamp from the upper left, pooled around the set; the desk fades into the dark.
        vec2 r = q - uSetCentre;
        float lamp = 0.6 + 1.4 * exp(-dot(r - vec2(-0.6, -0.2), r - vec2(-0.6, -0.2)) * 0.30);
        float far = exp(-max(t - length(uFloorPoint - ro), 0.0) * 0.10);
        vec3 desk = wood * lamp * mix(0.30, 1.0, far);
        // Blend into the wall near the horizon, so there is no hard edge.
        col = mix(wall, desk, smoothstep(0.0, 0.08, -denom));
    }
    fragColor = vec4(col, 1.0);
}
