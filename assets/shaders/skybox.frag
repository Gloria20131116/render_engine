#version 450 core
in vec3 vDirection;
out vec4 FragColor;

uniform samplerCube uEnvMap;
uniform float uLod;
uniform float uIntensity;

void main() {
    vec3 color = textureLod(uEnvMap, normalize(vDirection), uLod).rgb * uIntensity;
    FragColor = vec4(color, 1.0);
}
