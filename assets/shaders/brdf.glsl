// ============================================================================
//  brdf.glsl  --  Cook-Torrance BRDF building blocks.
//
//  This file is EDITABLE from the "BRDF Editor" panel in the engine UI.
//  Save it (or edit it in any text editor) and the PBR shader hot-reloads.
//
//  Selectable variants (driven by material uniforms):
//    uNDFType     : 0 = GGX (Trowbridge-Reitz), 1 = Beckmann, 2 = Blinn-Phong
//    uGeomType    : 0 = Smith height-correlated GGX, 1 = Smith Schlick-GGX,
//                   2 = Implicit
//    uFresnelType : 0 = Schlick, 1 = Schlick w/ roughness, 2 = none (F0)
// ============================================================================

const float PI = 3.14159265359;

// ---------------------------------------------------------------- NDF: D ---
float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 1e-7);
}

float D_Beckmann(float NdotH, float roughness) {
    float a = max(roughness * roughness, 1e-4);
    float a2 = a * a;
    float nh2 = NdotH * NdotH;
    return exp((nh2 - 1.0) / (a2 * nh2)) / max(PI * a2 * nh2 * nh2, 1e-7);
}

float D_BlinnPhong(float NdotH, float roughness) {
    float a = max(roughness * roughness, 1e-4);
    float a2 = a * a;
    float n = 2.0 / a2 - 2.0;
    return (n + 2.0) / (2.0 * PI) * pow(NdotH, n);
}

float Distribution(int type, float NdotH, float roughness) {
    if (type == 1) return D_Beckmann(NdotH, roughness);
    if (type == 2) return D_BlinnPhong(NdotH, roughness);
    return D_GGX(NdotH, roughness);
}

// ----------------------------------------------------- Geometry/Visibility ---
// Height-correlated Smith GGX (returns V = G / (4 NdotL NdotV))
float V_SmithGGXCorrelated(float NdotV, float NdotL, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float lambdaV = NdotL * sqrt(NdotV * NdotV * (1.0 - a2) + a2);
    float lambdaL = NdotV * sqrt(NdotL * NdotL * (1.0 - a2) + a2);
    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

float G_SchlickGGX(float NdotX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;  // direct lighting k
    return NdotX / (NdotX * (1.0 - k) + k);
}

float Visibility(int type, float NdotV, float NdotL, float roughness) {
    if (type == 1) {
        float G = G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
        return G / max(4.0 * NdotV * NdotL, 1e-5);
    }
    if (type == 2) {
        return 0.25;  // implicit: G = NdotL*NdotV, V = 1/4
    }
    return V_SmithGGXCorrelated(NdotV, NdotL, roughness);
}

// ------------------------------------------------------------- Fresnel: F ---
vec3 F_Schlick(vec3 F0, float VdotH) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

vec3 F_SchlickRoughness(vec3 F0, float VdotH, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) *
                    pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

vec3 Fresnel(int type, vec3 F0, float VdotH, float roughness) {
    if (type == 1) return F_SchlickRoughness(F0, VdotH, roughness);
    if (type == 2) return F0;
    return F_Schlick(F0, VdotH);
}

// ------------------------------------------------ Direct-light BRDF entry ---
// Returns the outgoing radiance for one light (specular + diffuse, already
// multiplied by NdotL). Feel free to rewrite this whole function!
vec3 EvalBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness,
              vec3 F0, int ndfType, int geomType, int fresnelType,
              float specularTint, float energyComp) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    float D = Distribution(ndfType, NdotH, roughness);
    float Vis = Visibility(geomType, NdotV, NdotL, roughness);
    vec3 F = Fresnel(fresnelType, F0, VdotH, roughness);

    vec3 specular = D * Vis * F;
    specular = mix(specular, specular * normalize(albedo + 1e-4), specularTint);
    specular *= energyComp;

    // Energy-conserving Lambert diffuse (killed for metals).
    vec3 kd = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kd * albedo / PI;

    return (diffuse + specular) * NdotL;
}
