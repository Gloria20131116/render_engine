#version 450 core
// 13-tap downsample (CoD: Advanced Warfare). First pass applies a soft-knee
// threshold and Karis average to suppress fireflies.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uSource;
uniform vec2 uSrcTexelSize;
uniform int uFirstPass;
uniform float uThreshold;
uniform float uKnee;

vec3 sampleSrc(vec2 uv) { return texture(uSource, uv).rgb; }

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

vec3 karisAvg(vec3 a, vec3 b, vec3 c, vec3 d) {
    float wa = 1.0 / (1.0 + luma(a));
    float wb = 1.0 / (1.0 + luma(b));
    float wc = 1.0 / (1.0 + luma(c));
    float wd = 1.0 / (1.0 + luma(d));
    return (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);
}

vec3 threshold(vec3 c) {
    float br = max(c.r, max(c.g, c.b));
    float knee = uThreshold * uKnee + 1e-5;
    float soft = clamp(br - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee);
    float contribution = max(soft, br - uThreshold) / max(br, 1e-5);
    return c * max(contribution, 0.0);
}

void main() {
    float x = uSrcTexelSize.x, y = uSrcTexelSize.y;
    vec2 uv = vUV;

    vec3 a = sampleSrc(uv + vec2(-2.0 * x,  2.0 * y));
    vec3 b = sampleSrc(uv + vec2(     0.0,  2.0 * y));
    vec3 c = sampleSrc(uv + vec2( 2.0 * x,  2.0 * y));
    vec3 d = sampleSrc(uv + vec2(-2.0 * x,      0.0));
    vec3 e = sampleSrc(uv);
    vec3 f = sampleSrc(uv + vec2( 2.0 * x,      0.0));
    vec3 g = sampleSrc(uv + vec2(-2.0 * x, -2.0 * y));
    vec3 h = sampleSrc(uv + vec2(     0.0, -2.0 * y));
    vec3 i = sampleSrc(uv + vec2( 2.0 * x, -2.0 * y));
    vec3 j = sampleSrc(uv + vec2(-x,  y));
    vec3 k = sampleSrc(uv + vec2( x,  y));
    vec3 l = sampleSrc(uv + vec2(-x, -y));
    vec3 m = sampleSrc(uv + vec2( x, -y));

    vec3 result;
    if (uFirstPass == 1) {
        vec3 g0 = karisAvg(j, k, l, m);
        vec3 g1 = karisAvg(a, b, d, e);
        vec3 g2 = karisAvg(b, c, e, f);
        vec3 g3 = karisAvg(d, e, g, h);
        vec3 g4 = karisAvg(e, f, h, i);
        result = g0 * 0.5 + (g1 + g2 + g3 + g4) * 0.125;
        result = threshold(result);
    } else {
        result = e * 0.125;
        result += (a + c + g + i) * 0.03125;
        result += (b + d + f + h) * 0.0625;
        result += (j + k + l + m) * 0.125;
    }
    FragColor = vec4(result, 1.0);
}
