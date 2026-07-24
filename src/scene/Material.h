#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>

class Texture;

enum class ShadingModel : int {
    PBR = 0,   // Cook-Torrance + IBL
    Toon = 1,  // ZZZ / Endfield style NPR (ramp shading, stylized specular, rim)
};

// BRDF term variants, selectable at runtime from the BRDF editor UI.
enum class NDFType : int { GGX = 0, Beckmann = 1, BlinnPhong = 2 };
enum class GeomType : int { SmithGGX = 0, SmithSchlickGGX = 1, Implicit = 2 };
enum class FresnelType : int { Schlick = 0, SchlickRoughness = 1, None = 2 };

struct Material {
    std::string name = "Material";
    ShadingModel model = ShadingModel::PBR;

    // ---- Common surface parameters ----
    glm::vec3 baseColor{0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
    glm::vec3 emissive{0.0f};
    float emissiveIntensity = 1.0f;
    float normalStrength = 1.0f;
    float specularF0 = 0.04f;  // dielectric reflectance at normal incidence
    float iblIntensity = 1.0f;
    float alphaCutoff = 0.0f;  // 0 = opaque, >0 = alpha test against albedo alpha

    // ---- BRDF variant selection (feeds brdf.glsl) ----
    NDFType ndf = NDFType::GGX;
    GeomType geom = GeomType::SmithGGX;
    FresnelType fresnel = FresnelType::Schlick;
    float specularTint = 0.0f;      // tint specular towards base color
    float energyCompensation = 1.0f;

    // ---- Toon (NPR) parameters ----
    glm::vec3 shadowColor{0.62f, 0.54f, 0.72f};  // multiply color in shade region
    float shadowThreshold = 0.5f;
    float shadowSoftness = 0.05f;
    float rampShift = 0.0f;         // second-tone offset
    glm::vec3 rimColor{1.0f};
    float rimWidth = 0.6f;
    float rimIntensity = 0.5f;
    float toonSpecSize = 0.1f;      // stylized specular lobe size
    float toonSpecIntensity = 0.6f;
    glm::vec3 toonSpecColor{1.0f};

    // ---- Outline (inverted hull, used by Toon materials) ----
    bool outline = false;
    float outlineWidth = 0.004f;    // approx. half-width in NDC (constant screen size)
    glm::vec3 outlineColor{0.05f, 0.03f, 0.06f};

    // ---- Texture slots (null = use scalar parameters) ----
    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> metallicMap;
    std::shared_ptr<Texture> roughnessMap;
    std::shared_ptr<Texture> aoMap;
    std::shared_ptr<Texture> emissiveMap;
    std::shared_ptr<Texture> rampMap;  // toon shading ramp (optional)

    bool doubleSided = false;
};
