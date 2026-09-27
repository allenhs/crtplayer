#version 330 core
// Full-viewport quad. All passes compute their coordinates from gl_FragCoord,
// so the vertex stage only needs to cover the viewport.
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main()
{
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
