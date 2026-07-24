#version 450 core
in vec3 vLocalPos;
out vec4 FragColor;

uniform sampler2D uEquirect;
uniform float uRotation;  // radians around Y

const vec2 invAtan = vec2(0.1591, 0.3183);

void main() {
    vec3 dir = normalize(vLocalPos);
    float c = cos(uRotation), s = sin(uRotation);
    dir = vec3(c * dir.x - s * dir.z, dir.y, s * dir.x + c * dir.z);
    vec2 uv = vec2(atan(dir.z, dir.x), asin(dir.y)) * invAtan + 0.5;
    vec3 color = texture(uEquirect, uv).rgb;
    FragColor = vec4(color, 1.0);
}
