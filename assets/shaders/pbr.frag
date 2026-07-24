#version 450 core
// Main forward shader: Cook-Torrance PBR + IBL, or NPR toon shading
// (ZZZ / Endfield style) selected per-material via uShadingModel.

#include "brdf.glsl"

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
    vec4 lightSpacePos;
} fs;

out vec4 FragColor;

// ---- Camera / matrices ----
uniform vec3 uCameraPos;

// ---- Material scalars ----
uniform int uShadingModel;  // 0 = PBR, 1 = Toon
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAO;
uniform vec3 uEmissive;
uniform float uNormalStrength;
uniform float uSpecularF0;
uniform float uIBLIntensity;
uniform float uAlphaCutoff;  // >0 = alpha test (Masked blend mode)
uniform float uOpacity;      // overall alpha multiplier (Transparent blend mode)
uniform int uFlipNormals;  // 1 = source asset normals point inward, flip them

// ---- BRDF variants ----
uniform int uNDFType;
uniform int uGeomType;
uniform int uFresnelType;
uniform float uSpecularTint;
uniform float uEnergyCompensation;

// ---- Toon parameters ----
uniform vec3 uShadowColor;
uniform float uShadowThreshold;
uniform float uShadowSoftness;
uniform float uRampShift;
uniform vec3 uRimColor;
uniform float uRimWidth;
uniform float uRimIntensity;
uniform float uToonSpecSize;
uniform float uToonSpecIntensity;
uniform vec3 uToonSpecColor;

// ---- Material textures ----
uniform int uHasAlbedoMap;    uniform sampler2D uAlbedoMap;
uniform int uHasNormalMap;    uniform sampler2D uNormalMap;
uniform int uHasMetallicMap;  uniform sampler2D uMetallicMap;
uniform int uHasRoughnessMap; uniform sampler2D uRoughnessMap;
uniform int uHasAOMap;        uniform sampler2D uAOMap;
uniform int uHasEmissiveMap;  uniform sampler2D uEmissiveMap;
uniform int uHasRampMap;      uniform sampler2D uRampMap;

// ---- Lights ----
uniform vec3 uSunDirection;  // from sun towards scene
uniform vec3 uSunColor;      // color * intensity
uniform int uSunCastShadows;
uniform float uShadowBias;
uniform sampler2D uShadowMap;

struct PointLight {
    vec3 position;
    vec3 color;   // color * intensity
    float radius;
};
uniform PointLight uPointLights[5];
uniform int uNumPointLights;

// ---- IBL ----
uniform samplerCube uIrradianceMap;
uniform samplerCube uPrefilterMap;
uniform sampler2D uBRDFLUT;
uniform int uPrefilterMips;
uniform float uEnvIntensity;

// ============================================================================

vec3 getNormal() {
    vec3 N = normalize(fs.normal);
    if (uHasNormalMap == 0) return N;
    vec3 T = normalize(fs.tangent.xyz - N * dot(N, fs.tangent.xyz));
    vec3 B = cross(N, T) * fs.tangent.w;
    vec3 nTex = texture(uNormalMap, fs.uv).rgb * 2.0 - 1.0;
    nTex.xy *= uNormalStrength;
    return normalize(mat3(T, B, N) * nTex);
}

float sunShadow(vec3 N) {
    if (uSunCastShadows == 0) return 1.0;
    vec3 proj = fs.lightSpacePos.xyz / fs.lightSpacePos.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0)
        return 1.0;
    float bias = max(uShadowBias * (1.0 - dot(N, -uSunDirection)) * 4.0, uShadowBias);
    // 3x3 PCF
    float shadow = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, proj.xy + vec2(x, y) * texel).r;
            shadow += (proj.z - bias) > depth ? 0.0 : 1.0;
        }
    return shadow / 9.0;
}

float pointAttenuation(float dist, float radius) {
    float att = 1.0 / max(dist * dist, 1e-4);
    float window = pow(clamp(1.0 - pow(dist / radius, 4.0), 0.0, 1.0), 2.0);
    return att * window;
}

// ---------------------------------------------------------------- PBR path
vec3 shadePBR(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 V) {
    vec3 F0 = mix(vec3(uSpecularF0), albedo, metallic);
    vec3 Lo = vec3(0.0);

    // Sun
    {
        vec3 L = -normalize(uSunDirection);
        vec3 radiance = uSunColor * sunShadow(N);
        Lo += EvalBRDF(N, V, L, albedo, metallic, roughness, F0, uNDFType, uGeomType,
                       uFresnelType, uSpecularTint, uEnergyCompensation) * radiance;
    }

    // Point lights
    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - fs.worldPos;
        float dist = length(toLight);
        if (dist > uPointLights[i].radius) continue;
        vec3 L = toLight / dist;
        vec3 radiance = uPointLights[i].color * pointAttenuation(dist, uPointLights[i].radius);
        Lo += EvalBRDF(N, V, L, albedo, metallic, roughness, F0, uNDFType, uGeomType,
                       uFresnelType, uSpecularTint, uEnergyCompensation) * radiance;
    }

    // IBL ambient
    float NdotV = max(dot(N, V), 1e-4);
    vec3 F = F_SchlickRoughness(F0, NdotV, roughness);
    vec3 kd = (1.0 - F) * (1.0 - metallic);

    vec3 irradiance = texture(uIrradianceMap, N).rgb;
    vec3 diffuseIBL = irradiance * albedo * kd;

    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * float(uPrefilterMips - 1)).rgb;
    vec2 envBRDF = texture(uBRDFLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefiltered * (F0 * envBRDF.x + envBRDF.y);

    vec3 ambient = (diffuseIBL + specularIBL) * uEnvIntensity * uIBLIntensity * ao;
    return Lo + ambient;
}

// ---------------------------------------------------------------- Toon path
vec3 shadeToon(vec3 albedo, float ao, vec3 N, vec3 V) {
    vec3 L = -normalize(uSunDirection);
    float NdotL = dot(N, L);
    float shadowTerm = sunShadow(N);
    float lambert = NdotL * 0.5 + 0.5;  // half-lambert
    lambert = min(lambert, shadowTerm * 0.5 + 0.5);

    // Two-tone ramp with soft transition (or sampled ramp texture)
    vec3 litColor = albedo;
    vec3 shadeColor = albedo * uShadowColor;
    float t = smoothstep(uShadowThreshold - uShadowSoftness,
                         uShadowThreshold + uShadowSoftness, lambert + uRampShift);
    vec3 color;
    if (uHasRampMap == 1) {
        vec3 ramp = texture(uRampMap, vec2(clamp(lambert + uRampShift, 0.02, 0.98), 0.5)).rgb;
        color = albedo * ramp;
    } else {
        color = mix(shadeColor, litColor, t);
    }
    color *= uSunColor / max(max(uSunColor.r, max(uSunColor.g, uSunColor.b)), 1e-3);

    // Stylized specular (sharp lobe)
    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float spec = smoothstep(1.0 - uToonSpecSize, 1.0 - uToonSpecSize + 0.02, NdotH);
    color += uToonSpecColor * spec * uToonSpecIntensity * t;

    // Fresnel rim light, biased towards the lit side
    float rim = pow(1.0 - max(dot(N, V), 0.0), 1.0 / max(uRimWidth, 1e-3));
    float rimMask = smoothstep(0.0, 0.4, NdotL * 0.5 + 0.5);
    color += uRimColor * rim * uRimIntensity * rimMask;

    // Point lights: simple stylized additive contribution
    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - fs.worldPos;
        float dist = length(toLight);
        if (dist > uPointLights[i].radius) continue;
        vec3 Lp = toLight / dist;
        float nl = smoothstep(0.0, 0.35, dot(N, Lp));
        color += albedo * uPointLights[i].color *
                 pointAttenuation(dist, uPointLights[i].radius) * nl * 0.35;
    }

    // A touch of ambient from the IBL irradiance to sit in the scene
    color += texture(uIrradianceMap, N).rgb * albedo * 0.15 * uEnvIntensity * uIBLIntensity;
    return color * ao;
}

void main() {
    vec4 albedoTex = uHasAlbedoMap == 1 ? texture(uAlbedoMap, fs.uv) : vec4(1.0);
    vec3 albedo = uBaseColor * albedoTex.rgb;
    if (uAlphaCutoff > 0.0 && albedoTex.a < uAlphaCutoff) discard;

    float metallic = uHasMetallicMap == 1 ? texture(uMetallicMap, fs.uv).b * uMetallic
                                          : uMetallic;
    float roughness = uHasRoughnessMap == 1 ? texture(uRoughnessMap, fs.uv).g * uRoughness
                                            : uRoughness;
    roughness = clamp(roughness, 0.02, 1.0);
    float ao = uHasAOMap == 1 ? texture(uAOMap, fs.uv).r * uAO : uAO;

    vec3 N = getNormal();
    if (uFlipNormals == 1) N = -N;
    if (!gl_FrontFacing) N = -N;
    vec3 V = normalize(uCameraPos - fs.worldPos);

    vec3 color = uShadingModel == 1 ? shadeToon(albedo, ao, N, V)
                                    : shadePBR(albedo, metallic, roughness, ao, N, V);

    vec3 emissive = uHasEmissiveMap == 1 ? texture(uEmissiveMap, fs.uv).rgb * uEmissive
                                         : uEmissive;
    color += emissive;

    // Alpha only matters in the blended transparent pass; the opaque pass
    // renders with blending disabled and ignores it.
    float alpha = clamp(uOpacity * albedoTex.a, 0.0, 1.0);
    FragColor = vec4(color, alpha);
}
