#version 450 core
// Main forward shader: Cook-Torrance PBR, NPR toon, and multi-lobe principled
// surfaces (Clearcoat / Cloth / Subsurface / Glass) selected via uShadingModel.

#include "brdf.glsl"
#include "principled.glsl"

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
uniform int uShadingModel;  // 0=PBR 1=Toon 2=Principled 3=Clearcoat 4=Cloth 5=Subsurface 6=Glass
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

// ---- Principled extras ----
uniform float uSheen;
uniform float uSheenTint;
uniform float uClearcoat;
uniform float uClearcoatGloss;
uniform float uAnisotropic;
uniform float uSubsurface;
uniform float uSpecular;
uniform float uTransmission;
uniform float uIOR;

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

void resolvePrincipledParams(inout float metallic, inout float sheen, inout float clearcoat,
                             inout float subsurface, inout float transmission) {
    if (uShadingModel == 3) {
        clearcoat = max(clearcoat, 0.6);
    } else if (uShadingModel == 4) {
        sheen = max(sheen, 0.7);
        metallic = 0.0;
        clearcoat = 0.0;
        transmission = 0.0;
    } else if (uShadingModel == 5) {
        subsurface = max(subsurface, 0.65);
        metallic = 0.0;
        transmission = 0.0;
    } else if (uShadingModel == 6) {
        transmission = max(transmission, 0.85);
        metallic = 0.0;
        sheen = 0.0;
        clearcoat = max(clearcoat, 0.15);
    }
}

vec3 shadePBR(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 V) {
    vec3 F0 = mix(vec3(uSpecularF0), albedo, metallic);
    vec3 Lo = vec3(0.0);

    {
        vec3 L = -normalize(uSunDirection);
        vec3 radiance = uSunColor * sunShadow(N);
        Lo += EvalBRDF(N, V, L, albedo, metallic, roughness, F0, uNDFType, uGeomType,
                       uFresnelType, uSpecularTint, uEnergyCompensation) * radiance;
    }

    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - fs.worldPos;
        float dist = length(toLight);
        if (dist > uPointLights[i].radius) continue;
        vec3 L = toLight / dist;
        vec3 radiance = uPointLights[i].color * pointAttenuation(dist, uPointLights[i].radius);
        Lo += EvalBRDF(N, V, L, albedo, metallic, roughness, F0, uNDFType, uGeomType,
                       uFresnelType, uSpecularTint, uEnergyCompensation) * radiance;
    }

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

vec3 shadePrincipled(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 V) {
    float sheen = uSheen;
    float clearcoat = uClearcoat;
    float subsurface = uSubsurface;
    float transmission = uTransmission;
    resolvePrincipledParams(metallic, sheen, clearcoat, subsurface, transmission);

    vec3 Lo = vec3(0.0);
    {
        vec3 L = -normalize(uSunDirection);
        vec3 radiance = uSunColor * sunShadow(N);
        Lo += EvalPrincipledBRDF(N, V, L, albedo, metallic, roughness, sheen, uSheenTint,
                                 clearcoat, uClearcoatGloss, uAnisotropic, subsurface, uSpecular,
                                 uSpecularTint, transmission, uIOR) *
              radiance;
    }
    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - fs.worldPos;
        float dist = length(toLight);
        if (dist > uPointLights[i].radius) continue;
        vec3 L = toLight / dist;
        vec3 radiance = uPointLights[i].color * pointAttenuation(dist, uPointLights[i].radius);
        Lo += EvalPrincipledBRDF(N, V, L, albedo, metallic, roughness, sheen, uSheenTint,
                                 clearcoat, uClearcoatGloss, uAnisotropic, subsurface, uSpecular,
                                 uSpecularTint, transmission, uIOR) *
              radiance;
    }

    float eta = max(uIOR, 1.0001);
    float r0 = (eta - 1.0) / (eta + 1.0);
    r0 = r0 * r0 * clamp(uSpecular * 2.0, 0.0, 1.0);
    vec3 tint = prin_Tint(albedo);
    vec3 F0 = mix(vec3(r0), tint * r0, clamp(uSpecularTint, 0.0, 1.0));
    F0 = mix(F0, albedo, metallic);

    float NdotV = max(dot(N, V), 1e-4);
    vec3 F = F_SchlickRoughness(F0, NdotV, roughness);
    float diffuseWeight = (1.0 - metallic) * (1.0 - transmission);
    vec3 diffuseIBL = texture(uIrradianceMap, N).rgb * albedo * (1.0 - F) * diffuseWeight;
    if (subsurface > 0.0) {
        vec3 back = texture(uIrradianceMap, -N).rgb * albedo;
        diffuseIBL = mix(diffuseIBL, mix(diffuseIBL, back, 0.45), subsurface);
    }
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * float(uPrefilterMips - 1)).rgb;
    vec2 envBRDF = texture(uBRDFLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefiltered * (F0 * envBRDF.x + envBRDF.y);
    if (clearcoat > 0.0) {
        vec3 coatPref = textureLod(uPrefilterMap, R, (1.0 - uClearcoatGloss) * 2.0).rgb;
        specularIBL += coatPref * (0.04 * envBRDF.x + envBRDF.y) * clearcoat * 0.25;
    }
    if (transmission > 0.0) {
        vec3 transIBL = texture(uIrradianceMap, -V).rgb * sqrt(max(albedo, vec3(0.0)));
        diffuseIBL += transIBL * transmission * (1.0 - F) * 0.65;
    }

    vec3 ambient = (diffuseIBL + specularIBL) * uEnvIntensity * uIBLIntensity * ao;
    return Lo + ambient;
}

vec3 shadeToon(vec3 albedo, float ao, vec3 N, vec3 V) {
    vec3 L = -normalize(uSunDirection);
    float NdotL = dot(N, L);
    float shadowTerm = sunShadow(N);
    float lambert = NdotL * 0.5 + 0.5;
    lambert = min(lambert, shadowTerm * 0.5 + 0.5);

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

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float spec = smoothstep(1.0 - uToonSpecSize, 1.0 - uToonSpecSize + 0.02, NdotH);
    color += uToonSpecColor * spec * uToonSpecIntensity * t;

    float rim = pow(1.0 - max(dot(N, V), 0.0), 1.0 / max(uRimWidth, 1e-3));
    float rimMask = smoothstep(0.0, 0.4, NdotL * 0.5 + 0.5);
    color += uRimColor * rim * uRimIntensity * rimMask;

    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - fs.worldPos;
        float dist = length(toLight);
        if (dist > uPointLights[i].radius) continue;
        vec3 Lp = toLight / dist;
        float nl = smoothstep(0.0, 0.35, dot(N, Lp));
        color += albedo * uPointLights[i].color *
                 pointAttenuation(dist, uPointLights[i].radius) * nl * 0.35;
    }

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

    vec3 color;
    if (uShadingModel == 1)
        color = shadeToon(albedo, ao, N, V);
    else if (uShadingModel >= 2)
        color = shadePrincipled(albedo, metallic, roughness, ao, N, V);
    else
        color = shadePBR(albedo, metallic, roughness, ao, N, V);

    vec3 emissive = uHasEmissiveMap == 1 ? texture(uEmissiveMap, fs.uv).rgb * uEmissive
                                         : uEmissive;
    color += emissive;

    float alpha = clamp(uOpacity * albedoTex.a, 0.0, 1.0);
    if (uShadingModel == 6 && uOpacity < 0.999) {
        float fres = prin_SchlickWeight(max(dot(N, V), 0.0));
        alpha = clamp(mix(uOpacity, 1.0, fres) * albedoTex.a, 0.0, 1.0);
    }
    FragColor = vec4(color, alpha);
}
