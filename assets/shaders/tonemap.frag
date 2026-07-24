#version 450 core
// Final post: exposure, bloom composite, tonemap (ACES fitted / Reinhard / none),
// then linear -> sRGB.
in vec2 vUV;
out vec4 FragColor;

uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform int uBloomEnabled;
uniform float uBloomIntensity;
uniform float uExposure;
uniform int uTonemapMode;  // 0 = ACES, 1 = Reinhard, 2 = none

// Stephen Hill's fitted ACES (sRGB working space)
const mat3 ACESInputMat = mat3(
    0.59719, 0.07600, 0.02840,
    0.35458, 0.90834, 0.13383,
    0.04823, 0.01566, 0.83777);

const mat3 ACESOutputMat = mat3(
     1.60475, -0.10208, -0.00327,
    -0.53108,  1.10813, -0.07276,
    -0.07367, -0.00605,  1.07602);

vec3 RRTAndODTFit(vec3 v) {
    vec3 a = v * (v + 0.0245786) - 0.000090537;
    vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
    return a / b;
}

vec3 ACESFitted(vec3 color) {
    color = ACESInputMat * color;
    color = RRTAndODTFit(color);
    color = ACESOutputMat * color;
    return clamp(color, 0.0, 1.0);
}

void main() {
    vec3 hdr = texture(uScene, vUV).rgb;
    if (uBloomEnabled == 1) {
        vec3 bloom = texture(uBloom, vUV).rgb;
        hdr = mix(hdr, bloom, uBloomIntensity);
        hdr += bloom * uBloomIntensity * 0.5;
    }
    hdr *= uExposure;

    vec3 ldr;
    if (uTonemapMode == 0)      ldr = ACESFitted(hdr);
    else if (uTonemapMode == 1) ldr = hdr / (1.0 + hdr);
    else                        ldr = clamp(hdr, 0.0, 1.0);

    ldr = pow(ldr, vec3(1.0 / 2.2));
    FragColor = vec4(ldr, 1.0);
}
