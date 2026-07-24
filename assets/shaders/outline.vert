#version 450 core
// Inverted-hull outline: extrude along normal in clip space, distance-compensated.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform float uWidth;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    mat3 normalMat = transpose(inverse(mat3(uModel)));
    vec3 nWorld = normalize(normalMat * aNormal);

    vec4 clip = uProj * uView * world;
    vec2 nView = normalize((mat3(uView) * nWorld).xy + vec2(1e-5));
    // uWidth is roughly the outline half-width in NDC (constant screen size).
    clip.xy += nView * uWidth * clip.w;
    gl_Position = clip;
}
