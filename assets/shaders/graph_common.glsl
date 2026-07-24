// Shared template for material-graph generated shaders.
// Provides: vertex inputs, scene/light/IBL uniforms (same names as pbr.frag
// so the renderer shares its binding code), lighting evaluators and the g_*
// helper library used by graph nodes.

#include "brdf.glsl"

// HLSL compatibility aliases so UE-style Custom node code ports directly.
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define lerp mix
#define saturate(x) clamp((x), 0.0, 1.0)
#define frac fract
#define mul(a, b) ((a) * (b))
#define rsqrt inversesqrt

in VS_OUT {
    vec3 worldPos;
    vec3 normal;
    vec2 uv;
    vec4 tangent;
    vec4 lightSpacePos;
} fs;

out vec4 FragColor;

// ---- Scene ----
uniform vec3 uCameraPos;
uniform mat4 uModel;        // for model-space helpers (SDF face shadow)
uniform int uShadingModel;  // 0 = PBR, 1 = Toon
uniform int uFlipNormals;
uniform float uTime;
uniform vec2 uViewportSize;
uniform float uSpecularF0;  // dielectric F0 from the material

// ---- Lights (identical names/layout to pbr.frag) ----
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform int uSunCastShadows;
uniform float uShadowBias;
uniform sampler2D uShadowMap;

struct PointLight {
    vec3 position;
    vec3 color;
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
uniform float uIBLIntensity;

// ---- Globals prepared once per fragment (used by node expressions) ----
vec3 gNgeom;  // geometric world normal (front-facing corrected)
vec3 gN;      // alias of gNgeom, what "WorldNormal" nodes read
vec3 gV;      // view direction
vec2 gUV;

void g_prepare() {
    gNgeom = normalize(fs.normal);
    if (uFlipNormals == 1) gNgeom = -gNgeom;
    if (!gl_FrontFacing) gNgeom = -gNgeom;
    gN = gNgeom;
    gV = normalize(uCameraPos - fs.worldPos);
    gUV = fs.uv;
}

// ============================================================================
// Shadow / attenuation (mirrors pbr.frag)
// ============================================================================

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

// ============================================================================
// Lighting evaluators (fixed GGX/Smith/Schlick BRDF variants)
// ============================================================================

vec3 evaluatePBR(vec3 albedo, float metallic, float roughness, float ao, vec3 N, vec3 V) {
    float f0s = uSpecularF0 > 0.0 ? uSpecularF0 : 0.04;
    vec3 F0 = mix(vec3(f0s), albedo, metallic);
    vec3 Lo = vec3(0.0);

    {
        vec3 L = -normalize(uSunDirection);
        vec3 radiance = uSunColor * sunShadow(N);
        Lo += EvalBRDF(N, V, L, albedo, metallic, roughness, F0, 0, 0, 0, 0.0, 1.0) * radiance;
    }
    for (int i = 0; i < uNumPointLights; ++i) {
        vec3 toLight = uPointLights[i].position - fs.worldPos;
        float dist = length(toLight);
        if (dist > uPointLights[i].radius) continue;
        vec3 L = toLight / dist;
        vec3 radiance = uPointLights[i].color * pointAttenuation(dist, uPointLights[i].radius);
        Lo += EvalBRDF(N, V, L, albedo, metallic, roughness, F0, 0, 0, 0, 0.0, 1.0) * radiance;
    }

    float NdotV = max(dot(N, V), 1e-4);
    vec3 F = F_SchlickRoughness(F0, NdotV, roughness);
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuseIBL = texture(uIrradianceMap, N).rgb * albedo * kd;
    vec3 R = reflect(-V, N);
    vec3 prefiltered = textureLod(uPrefilterMap, R, roughness * float(uPrefilterMips - 1)).rgb;
    vec2 envBRDF = texture(uBRDFLUT, vec2(NdotV, roughness)).rg;
    vec3 specularIBL = prefiltered * (F0 * envBRDF.x + envBRDF.y);
    vec3 ambient = (diffuseIBL + specularIBL) * uEnvIntensity * uIBLIntensity * ao;
    return Lo + ambient;
}

vec3 evaluateToon(vec3 albedo, float ao, vec3 N, vec3 V, vec3 shadowColor, float shadowThreshold,
                  float shadowSoftness, vec3 rimColor, float rimWidth, float rimIntensity,
                  vec3 specColor, float specSize, float specIntensity) {
    vec3 L = -normalize(uSunDirection);
    float NdotL = dot(N, L);
    float shadowTerm = sunShadow(N);
    float lambert = NdotL * 0.5 + 0.5;
    lambert = min(lambert, shadowTerm * 0.5 + 0.5);

    float t = smoothstep(shadowThreshold - shadowSoftness, shadowThreshold + shadowSoftness,
                         lambert);
    vec3 color = mix(albedo * shadowColor, albedo, t);
    color *= uSunColor / max(max(uSunColor.r, max(uSunColor.g, uSunColor.b)), 1e-3);

    vec3 H = normalize(V + L);
    float NdotH = max(dot(N, H), 0.0);
    float spec = smoothstep(1.0 - specSize, 1.0 - specSize + 0.02, NdotH);
    color += specColor * spec * specIntensity * t;

    float rim = pow(1.0 - max(dot(N, V), 0.0), 1.0 / max(rimWidth, 1e-3));
    float rimMask = smoothstep(0.0, 0.4, NdotL * 0.5 + 0.5);
    color += rimColor * rim * rimIntensity * rimMask;

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

// ============================================================================
// Node helper library (g_*)
// ============================================================================

vec2 g_rotateUV(vec2 uv, float angle, vec2 center) {
    float s = sin(angle), c = cos(angle);
    vec2 p = uv - center;
    return vec2(p.x * c - p.y * s, p.x * s + p.y * c) + center;
}

// ---- Noise ----
float g_hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float g_valueNoise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = g_hash21(i);
    float b = g_hash21(i + vec2(1, 0));
    float c = g_hash21(i + vec2(0, 1));
    float d = g_hash21(i + vec2(1, 1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

vec2 g_hash22(vec2 p) {
    return vec2(g_hash21(p), g_hash21(p + 17.17));
}

float g_perlin(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = dot(g_hash22(i) * 2.0 - 1.0, f);
    float b = dot(g_hash22(i + vec2(1, 0)) * 2.0 - 1.0, f - vec2(1, 0));
    float c = dot(g_hash22(i + vec2(0, 1)) * 2.0 - 1.0, f - vec2(0, 1));
    float d = dot(g_hash22(i + vec2(1, 1)) * 2.0 - 1.0, f - vec2(1, 1));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 0.5 + 0.5;
}

float g_voronoi(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float minDist = 8.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x) {
            vec2 cell = vec2(x, y);
            vec2 pt = g_hash22(i + cell);
            minDist = min(minDist, length(cell + pt - f));
        }
    return clamp(minDist, 0.0, 1.0);
}

// ---- Toon / NPR helpers ----

// SDF face shadow (ZZZ / Genshin style). The SDF map encodes, per texel, the
// sun angle threshold at which the point falls into shadow. Face forward/right
// axes come from the model matrix (+Z forward, +X right convention).
float g_sdfFaceShadow(float sdf, float softness) {
    vec3 fwd = normalize(mat3(uModel) * vec3(0.0, 0.0, 1.0));
    vec3 right = normalize(mat3(uModel) * vec3(1.0, 0.0, 0.0));
    vec3 L = -normalize(uSunDirection);
    vec2 L2 = normalize(vec2(dot(L, right), dot(L, fwd)) + vec2(1e-5));
    // Angle of light around the head, remapped to [0,1] where 0 = frontal.
    float angle = acos(clamp(L2.y, -1.0, 1.0)) / 3.14159265;
    float s = L2.x < 0.0 ? sdf : 1.0 - sdf;  // mirror for the other half-face
    return smoothstep(angle - softness, angle + softness, s);
}

// Kajiya-Kay anisotropic strand highlight along the tangent direction.
float g_kajiyaKay(float shift, float exponent) {
    vec3 T = normalize(fs.tangent.xyz + gNgeom * shift);
    vec3 L = -normalize(uSunDirection);
    vec3 H = normalize(L + gV);
    float TdotH = dot(T, H);
    float sinTH = sqrt(max(1.0 - TdotH * TdotH, 0.0));
    return pow(sinTH, max(exponent, 1.0));
}

float g_stylizedSpec(float size, float hardness) {
    vec3 L = -normalize(uSunDirection);
    vec3 H = normalize(gV + L);
    float NdotH = max(dot(gN, H), 0.0);
    return smoothstep(1.0 - size, 1.0 - size + max(hardness, 1e-4), NdotH);
}

float g_rim(float width, float litSideBias) {
    float rim = pow(1.0 - max(dot(gN, gV), 0.0), 1.0 / max(width, 1e-3));
    float NdotL = dot(gN, -normalize(uSunDirection));
    float mask = mix(1.0, smoothstep(0.0, 0.4, NdotL * 0.5 + 0.5), clamp(litSideBias, 0.0, 1.0));
    return rim * mask;
}

// View-space normal -> matcap UV.
vec2 g_matcapUV() {
    // Reconstruct view-space normal from world normal using the view basis
    // implied by gV; approximate with camera-facing projection.
    vec3 up = abs(gV.y) > 0.99 ? vec3(0, 0, 1) : vec3(0, 1, 0);
    vec3 right = normalize(cross(up, gV));
    vec3 upv = cross(gV, right);
    return vec2(dot(gN, right), dot(gN, upv)) * 0.5 + 0.5;
}

// Cheap wrap-lighting subsurface approximation.
vec3 g_sss(vec3 sssColor, float wrap) {
    vec3 L = -normalize(uSunDirection);
    float NdotL = dot(gN, L);
    float wrapped = clamp((NdotL + wrap) / (1.0 + wrap), 0.0, 1.0);
    float scatter = smoothstep(0.0, 1.0, wrapped) * (1.0 - smoothstep(0.0, 0.6, abs(NdotL)));
    return sssColor * scatter * uSunColor;
}
