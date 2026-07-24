#version 450 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
uniform mat4 uLightMatrix;

out VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
    vec4 lightSpacePos;
} vs;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vs.worldPos = world.xyz;
    mat3 normalMat = transpose(inverse(mat3(uModel)));
    vs.normal = normalize(normalMat * aNormal);
    vs.tangent = vec4(normalize(mat3(uModel) * aTangent.xyz), aTangent.w);
    vs.uv = aUV;
    vs.lightSpacePos = uLightMatrix * world;
    gl_Position = uProj * uView * world;
}
