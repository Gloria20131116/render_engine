#version 450 core
in vec2 vUV;
out vec4 FragColor;

uniform vec3 uColor;           // fixed outline color
uniform int uFromBaseColor;    // 1 = derive from (darkened) base color, ZZZ-style
uniform float uColorScale;     // darkening multiplier when deriving
uniform vec3 uBaseColor;
uniform int uHasAlbedoMap;
uniform sampler2D uAlbedoMap;

void main() {
    vec3 color = uColor;
    if (uFromBaseColor == 1) {
        vec3 base = uBaseColor;
        if (uHasAlbedoMap == 1) base *= texture(uAlbedoMap, vUV).rgb;
        color = base * uColorScale;
    }
    FragColor = vec4(color, 1.0);
}
