#version 450 core
// 9-tap tent filter upsample, blended additively into the larger mip.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;
uniform vec2 uSrcTexelSize;
uniform float uRadius;

void main() {
    float x = uSrcTexelSize.x * uRadius;
    float y = uSrcTexelSize.y * uRadius;

    vec3 a = texture(uSource, vUV + vec2(-x,  y)).rgb;
    vec3 b = texture(uSource, vUV + vec2( 0,  y)).rgb;
    vec3 c = texture(uSource, vUV + vec2( x,  y)).rgb;
    vec3 d = texture(uSource, vUV + vec2(-x,  0)).rgb;
    vec3 e = texture(uSource, vUV).rgb;
    vec3 f = texture(uSource, vUV + vec2( x,  0)).rgb;
    vec3 g = texture(uSource, vUV + vec2(-x, -y)).rgb;
    vec3 h = texture(uSource, vUV + vec2( 0, -y)).rgb;
    vec3 i = texture(uSource, vUV + vec2( x, -y)).rgb;

    vec3 result = e * 4.0 + (b + d + f + h) * 2.0 + (a + c + g + i);
    FragColor = vec4(result / 16.0, 1.0);
}
