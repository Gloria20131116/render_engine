// ============================================================================
//  principled.glsl  --  Multi-lobe artist-friendly surface BRDF.
//
//  Real-time adaptation of the production principled surface model described in
//  the SIGGRAPH 2012/2015 PBR courses (Selas / Burley reference implementation).
//  Lobe layout: diffuse (+ soft subsurface), sheen, anisotropic specular,
//  clearcoat, and a thin-glass transmission approximation.
// ============================================================================

float prin_SchlickWeight(float cosTheta) {
    float m = clamp(1.0 - cosTheta, 0.0, 1.0);
    float m2 = m * m;
    return m2 * m2 * m;
}

float prin_GTR1(float NdotH, float a) {
    if (a >= 1.0) return 1.0 / PI;
    float a2 = a * a;
    float t = 1.0 + (a2 - 1.0) * NdotH * NdotH;
    return (a2 - 1.0) / (PI * log(a2) * t);
}

float prin_GTR2Aniso(float NdotH, float HdotX, float HdotY, float ax, float ay) {
    float a = HdotX / ax;
    float b = HdotY / ay;
    float c = a * a + b * b + NdotH * NdotH;
    return 1.0 / max(PI * ax * ay * c * c, 1e-7);
}

float prin_SmithG1Aniso(float NdotV, float VdotX, float VdotY, float ax, float ay) {
    float a = VdotX * ax;
    float b = VdotY * ay;
    float c = NdotV;
    return 2.0 * NdotV / max(NdotV + sqrt(a * a + b * b + c * c), 1e-5);
}

float prin_SmithG1Iso(float NdotX, float alpha) {
    float a = alpha * alpha;
    float b = NdotX * NdotX;
    return 2.0 * NdotX / max(NdotX + sqrt(a + b - a * b), 1e-5);
}

vec3 prin_Tint(vec3 baseColor) {
    float lum = dot(baseColor, vec3(0.3, 0.6, 0.1));
    return lum > 0.0 ? baseColor / lum : vec3(1.0);
}

void prin_AnisoAxes(float roughness, float anisotropic, out float ax, out float ay) {
    float aspect = sqrt(1.0 - 0.9 * clamp(anisotropic, 0.0, 1.0));
    float r2 = roughness * roughness;
    ax = max(0.001, r2 / aspect);
    ay = max(0.001, r2 * aspect);
}

// Build an orthonormal tangent frame from N (stable enough for lookdev).
void prin_Frame(vec3 N, out vec3 T, out vec3 B) {
    vec3 up = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
    T = normalize(cross(up, N));
    B = cross(N, T);
}

vec3 prin_EvalSheen(vec3 baseColor, float sheen, float sheenTint, float HdotL) {
    if (sheen <= 0.0) return vec3(0.0);
    vec3 tint = mix(vec3(1.0), prin_Tint(baseColor), sheenTint);
    return sheen * tint * prin_SchlickWeight(abs(HdotL));
}

float prin_EvalClearcoat(float clearcoat, float gloss, float NdotH, float NdotL, float NdotV,
                         float HdotL) {
    if (clearcoat <= 0.0 || NdotL <= 0.0 || NdotV <= 0.0) return 0.0;
    float a = mix(0.1, 0.001, clamp(gloss, 0.0, 1.0));
    float D = prin_GTR1(NdotH, a);
    float F = mix(0.04, 1.0, prin_SchlickWeight(HdotL));
    float G = prin_SmithG1Iso(NdotL, 0.25) * prin_SmithG1Iso(NdotV, 0.25);
    return 0.25 * clearcoat * D * F * G;
}

float prin_EvalDiffuse(float roughness, float subsurface, float NdotL, float NdotV, float HdotL) {
    float fl = prin_SchlickWeight(NdotL);
    float fv = prin_SchlickWeight(NdotV);
    float rr = 0.5 + 2.0 * HdotL * HdotL * roughness * roughness;
    float retro = rr * (fl + fv + fl * fv * (rr - 1.0));

    // Soft subsurface (thin / Hanrahan-Krueger style mix)
    float fss90 = HdotL * HdotL * roughness * roughness;
    float fss = mix(1.0, fss90, fl) * mix(1.0, fss90, fv);
    float ss = 1.25 * (fss * (1.0 / max(NdotL + NdotV, 1e-4) - 0.5) + 0.5);
    float diffuse = mix(1.0, ss, clamp(subsurface, 0.0, 1.0));

    return (1.0 / PI) * (retro + diffuse * (1.0 - 0.5 * fl) * (1.0 - 0.5 * fv));
}

vec3 prin_EvalSpecular(vec3 baseColor, float metallic, float roughness, float anisotropic,
                       float specular, float specularTint, float ior,
                       vec3 N, vec3 V, vec3 L, vec3 H, vec3 T, vec3 B,
                       float NdotL, float NdotV, float NdotH, float HdotL) {
    if (NdotL <= 0.0 || NdotV <= 0.0) return vec3(0.0);

    float ax, ay;
    prin_AnisoAxes(roughness, anisotropic, ax, ay);
    float HdotX = dot(H, T);
    float HdotY = dot(H, B);
    float LdotX = dot(L, T);
    float LdotY = dot(L, B);
    float VdotX = dot(V, T);
    float VdotY = dot(V, B);

    float D = prin_GTR2Aniso(NdotH, HdotX, HdotY, ax, ay);
    float G = prin_SmithG1Aniso(NdotL, LdotX, LdotY, ax, ay) *
              prin_SmithG1Aniso(NdotV, VdotX, VdotY, ax, ay);

    // Dielectric R0 from IOR, scaled by specular, then tinted; lerp to baseColor for metal.
    float eta = max(ior, 1.0001);
    float r0 = (eta - 1.0) / (eta + 1.0);
    r0 = r0 * r0 * clamp(specular * 2.0, 0.0, 1.0);  // specular=0.5 -> full R0
    vec3 tint = prin_Tint(baseColor);
    vec3 F0 = mix(vec3(r0), tint * r0, clamp(specularTint, 0.0, 1.0));
    F0 = mix(F0, baseColor, metallic);
    vec3 F = F0 + (1.0 - F0) * prin_SchlickWeight(HdotL);

    return D * G * F / max(4.0 * NdotL * NdotV, 1e-5);
}

// Thin-glass transmission approximation for lookdev (same-hemisphere fake).
vec3 prin_EvalThinTransmission(vec3 baseColor, float transmission, float roughness, float ior,
                               float NdotL, float NdotV, float HdotL) {
    if (transmission <= 0.0 || NdotL <= 0.0) return vec3(0.0);
    float F = mix(0.0, 1.0, prin_SchlickWeight(abs(HdotL)));
    // Roughness softens the transmitted highlight; color uses sqrt(base) like thin surfaces.
    float soft = mix(1.0, NdotL, 1.0 - roughness);
    return transmission * sqrt(max(baseColor, vec3(0.0))) * (1.0 - F) * soft / PI;
}

// Full multi-lobe BRDF * NdotL  (direct lighting contribution, radiance separate).
vec3 EvalPrincipledBRDF(vec3 N, vec3 V, vec3 L, vec3 albedo, float metallic, float roughness,
                        float sheen, float sheenTint, float clearcoat, float clearcoatGloss,
                        float anisotropic, float subsurface, float specular, float specularTint,
                        float transmission, float ior) {
    float NdotL = max(dot(N, L), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);

    vec3 H = normalize(V + L);
    float NdotV = max(dot(N, V), 1e-4);
    float NdotH = max(dot(N, H), 0.0);
    float HdotL = max(dot(H, L), 0.0);

    vec3 T, B;
    prin_Frame(N, T, B);

    float diffuseWeight = (1.0 - metallic) * (1.0 - transmission);
    float transWeight = (1.0 - metallic) * transmission;

    vec3 f = vec3(0.0);

    if (diffuseWeight > 0.0) {
        float diff = prin_EvalDiffuse(roughness, subsurface, NdotL, NdotV, HdotL);
        vec3 she = prin_EvalSheen(albedo, sheen, sheenTint, HdotL);
        f += diffuseWeight * (albedo * diff + she);
    }

    f += prin_EvalSpecular(albedo, metallic, roughness, anisotropic, specular, specularTint, ior,
                           N, V, L, H, T, B, NdotL, NdotV, NdotH, HdotL);

    float coat = prin_EvalClearcoat(clearcoat, clearcoatGloss, NdotH, NdotL, NdotV, HdotL);
    f += vec3(coat);

    if (transWeight > 0.0)
        f += transWeight * prin_EvalThinTransmission(albedo, 1.0, roughness, ior, NdotL, NdotV, HdotL);

    return f * NdotL;
}
