#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <string>

class Texture;

enum class ShadingModel : int {
    PBR = 0,         // Cook-Torrance + IBL
    Toon = 1,        // NPR ramp / stylized specular / rim
    Principled = 2,  // Artist-friendly multi-lobe surface (diffuse + specular + sheen)
    Clearcoat = 3,   // Principled + clearcoat layer
    Cloth = 4,       // Sheen-forward cloth / velvet
    Subsurface = 5,  // Soft subsurface-style diffuse
    Glass = 6,       // Thin-glass style specular + transmission approx
};

// BRDF term variants, selectable at runtime from the BRDF editor UI.
enum class NDFType : int { GGX = 0, Beckmann = 1, BlinnPhong = 2 };
enum class GeomType : int { SmithGGX = 0, SmithSchlickGGX = 1, Implicit = 2 };
enum class FresnelType : int { Schlick = 0, SchlickRoughness = 1, None = 2 };

// How the material is composited into the scene.
enum class BlendMode : int {
    Opaque = 0,       // depth-tested, depth-written, no blending
    Masked = 1,       // alpha test: discard below alphaCutoff (hard edges, casts shadows)
    Transparent = 2,  // alpha blend: sorted back-to-front pass, no depth write / shadows
};

// Per-material depth-write override. Auto follows the blend mode:
// on for Opaque/Masked, off for Transparent.
enum class DepthWriteMode : int { Auto = 0, On = 1, Off = 2 };

struct Material {
    std::string name = "Material";
    std::string assetPath;  // .mat file path (empty = unsaved)
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

    // ---- Blending / transparency ----
    BlendMode blend = BlendMode::Opaque;
    float alphaCutoff = 0.5f;  // Masked: discard when albedo alpha < cutoff
    float opacity = 1.0f;      // Transparent: overall alpha multiplier

    // ---- Depth / sorting control ----
    int sortPriority = 0;      // higher draws later (on top); transparent sorts by
                               // (priority, then back-to-front distance)
    bool depthTest = true;     // off = always draw on top (overlays, x-ray weapons)
    DepthWriteMode depthWrite = DepthWriteMode::Auto;
    float depthBias = 0.0f;    // polygon offset; negative pulls toward camera (decals)

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
    float outlineWidthPx = 3.0f;       // constant width in screen pixels
    float outlineMaxWorldWidth = 0.03f;  // clamp extrusion in world units (close-ups)
    float outlineZOffset = 0.0002f;    // depth push to avoid z-fighting
    glm::vec3 outlineColor{0.05f, 0.03f, 0.06f};
    bool outlineFromBaseColor = true;  // derive color from darkened base color
    float outlineColorScale = 0.25f;   // multiplier when deriving from base

    // ---- Principled multi-lobe extras (Clearcoat / Cloth / Subsurface / Glass) ----
    float sheen = 0.0f;            // grazing-angle velvet / cloth highlight
    float sheenTint = 0.5f;        // 0 = white sheen, 1 = tinted by base color
    float clearcoat = 0.0f;        // secondary clearcoat layer weight
    float clearcoatGloss = 1.0f;   // 1 = sharp coat, 0 = rough coat
    float anisotropic = 0.0f;      // specular anisotropy (-ish stretch)
    float subsurface = 0.0f;       // soft SSS-style diffuse mix
    float specular = 0.5f;         // dielectric specular intensity (maps to F0 scale)
    float transmission = 0.0f;     // thin transmission weight (Glass)
    float ior = 1.5f;              // index of refraction for Glass / dielectrics

    // ---- Texture slots (null = use scalar parameters) ----
    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> metallicMap;
    std::shared_ptr<Texture> roughnessMap;
    std::shared_ptr<Texture> aoMap;
    std::shared_ptr<Texture> emissiveMap;
    std::shared_ptr<Texture> rampMap;  // toon shading ramp (optional)

    bool doubleSided = false;
    bool flipNormals = false;  // for assets whose source normals point inward

    // Optional node graph (UE-style material editor). When set, the renderer
    // uses the graph-generated shader instead of the unified pbr shader for
    // surface parameters; lighting model is still selected by `model`.
    std::shared_ptr<class MaterialGraph> graph;
};
