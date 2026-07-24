#version 450 core
// ZZZ / Genshin-style inverted hull outline:
//  - extrude along SMOOTHED normals (attribute 4) so hard edges / UV seams
//    don't tear the hull apart,
//  - width is constant in SCREEN PIXELS (compensated by w and viewport size),
//    clamped in world units so close-ups don't become absurdly thick,
//  - small depth offset pushes the hull behind the surface to avoid z-fighting.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 4) in vec3 aSmoothNormal;

out vec2 vUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform vec2 uViewport;   // pixels
uniform float uWidthPx;   // outline width in pixels
uniform float uMaxWorldWidth;  // clamp: max extrusion in world units
uniform float uZOffset;   // NDC-ish depth push (positive = away from camera)
uniform int uFlipNormals; // 1 = source asset normals point inward

void main() {
    mat4 mv = uView * uModel;
    vec4 viewPos = mv * vec4(aPosition, 1.0);
    mat3 normalMat = transpose(inverse(mat3(mv)));
    vec3 smoothN = uFlipNormals == 1 ? -aSmoothNormal : aSmoothNormal;
    vec3 nView = normalize(normalMat * smoothN);

    vec4 clip = uProj * viewPos;

    // Screen-space extrusion direction from the view-space normal.
    vec3 nClip = mat3(uProj) * nView;
    vec2 dir = nClip.xy;
    float len = length(dir);
    dir = len > 1e-5 ? dir / len : vec2(0.0);

    // Pixel width -> NDC offset (before perspective divide multiply by w).
    vec2 ndcPerPixel = 2.0 / uViewport;
    vec2 offset = dir * uWidthPx * ndcPerPixel * clip.w;

    // Clamp the equivalent world-space extrusion so near-camera outlines
    // don't blow up (w is proportional to view depth).
    float worldWidth = uWidthPx * ndcPerPixel.y * clip.w / uProj[1][1];
    if (worldWidth > uMaxWorldWidth && worldWidth > 0.0)
        offset *= uMaxWorldWidth / worldWidth;

    clip.xy += offset;
    clip.z += uZOffset * clip.w;
    gl_Position = clip;
    vUV = aUV;
}
