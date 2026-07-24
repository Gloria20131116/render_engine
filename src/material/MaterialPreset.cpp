#include "material/MaterialPreset.h"

#include <functional>

#include "scene/Material.h"

namespace {

struct PresetEntry {
    const char* name;
    std::function<Material()> factory;
};

Material base(const char* name) {
    Material m;
    m.name = name;
    return m;
}

const std::vector<PresetEntry>& registry() {
    static const std::vector<PresetEntry> presets = {
        {"Default PBR",
         [] {
             Material m = base("Default PBR");
             m.baseColor = {0.8f, 0.8f, 0.8f};
             m.metallic = 0.0f;
             m.roughness = 0.5f;
             return m;
         }},
        {"Metal (Polished)",
         [] {
             Material m = base("Metal (Polished)");
             m.baseColor = {0.95f, 0.93f, 0.88f};
             m.metallic = 1.0f;
             m.roughness = 0.15f;
             return m;
         }},
        {"Metal (Brushed)",
         [] {
             Material m = base("Metal (Brushed)");
             m.baseColor = {0.91f, 0.92f, 0.92f};
             m.metallic = 1.0f;
             m.roughness = 0.45f;
             return m;
         }},
        {"Glass (Approx)",
         [] {
             Material m = base("Glass (Approx)");
             m.baseColor = {0.9f, 0.95f, 1.0f};
             m.metallic = 0.0f;
             m.roughness = 0.03f;
             m.specularF0 = 0.08f;  // higher IOR than default dielectric
             m.iblIntensity = 1.5f;
             m.doubleSided = true;
             m.blend = BlendMode::Transparent;
             m.opacity = 0.35f;
             return m;
         }},
        {"Skin (SSS Approx)",
         [] {
             Material m = base("Skin (SSS Approx)");
             m.baseColor = {0.87f, 0.68f, 0.58f};
             m.metallic = 0.0f;
             m.roughness = 0.55f;
             m.specularF0 = 0.028f;  // skin F0 ~0.028
             // Warm shadow tint fakes light bleeding through skin.
             m.shadowColor = {0.85f, 0.52f, 0.46f};
             return m;
         }},
        {"Cloth",
         [] {
             Material m = base("Cloth");
             m.baseColor = {0.55f, 0.55f, 0.6f};
             m.metallic = 0.0f;
             m.roughness = 0.92f;
             m.specularF0 = 0.02f;
             m.energyCompensation = 1.15f;  // fake sheen: boost grazing response
             return m;
         }},
        {"Emissive",
         [] {
             Material m = base("Emissive");
             m.baseColor = {0.05f, 0.05f, 0.05f};
             m.roughness = 0.8f;
             m.emissive = {1.0f, 0.6f, 0.25f};
             m.emissiveIntensity = 6.0f;
             return m;
         }},
        {"Toon Default (ZZZ)",
         [] {
             Material m = base("Toon Default (ZZZ)");
             m.model = ShadingModel::Toon;
             m.baseColor = {0.93f, 0.82f, 0.78f};
             m.outline = true;
             return m;
         }},
        {"Toon Skin",
         [] {
             Material m = base("Toon Skin");
             m.model = ShadingModel::Toon;
             m.baseColor = {0.99f, 0.87f, 0.80f};
             m.shadowColor = {0.94f, 0.66f, 0.62f};  // warm skin shade tone
             m.shadowThreshold = 0.55f;
             m.shadowSoftness = 0.02f;
             m.rimIntensity = 0.35f;
             m.toonSpecIntensity = 0.0f;  // skin has no hard specular blob
             m.outline = true;
             m.outlineColorScale = 0.45f;
             return m;
         }},
        {"Toon Hair",
         [] {
             Material m = base("Toon Hair");
             m.model = ShadingModel::Toon;
             m.baseColor = {0.35f, 0.32f, 0.42f};
             m.shadowColor = {0.55f, 0.45f, 0.7f};
             m.shadowThreshold = 0.5f;
             m.shadowSoftness = 0.04f;
             // Wide bright band approximates the anime hair "angel ring".
             m.toonSpecSize = 0.32f;
             m.toonSpecIntensity = 1.4f;
             m.toonSpecColor = {1.0f, 0.95f, 0.9f};
             m.rimIntensity = 0.7f;
             m.outline = true;
             return m;
         }},
        {"Toon Cloth",
         [] {
             Material m = base("Toon Cloth");
             m.model = ShadingModel::Toon;
             m.baseColor = {0.82f, 0.83f, 0.88f};
             m.shadowColor = {0.58f, 0.55f, 0.72f};
             m.shadowThreshold = 0.48f;
             m.shadowSoftness = 0.06f;
             m.toonSpecIntensity = 0.25f;
             m.rimIntensity = 0.4f;
             m.outline = true;
             return m;
         }},
    };
    return presets;
}

}  // namespace

const std::vector<std::string>& MaterialPreset::names() {
    static const std::vector<std::string> list = [] {
        std::vector<std::string> v;
        for (const auto& p : registry()) v.push_back(p.name);
        return v;
    }();
    return list;
}

int MaterialPreset::count() { return (int)registry().size(); }

Material MaterialPreset::create(int index) {
    const auto& r = registry();
    if (index < 0 || index >= (int)r.size()) index = 0;
    return r[index].factory();
}

Material MaterialPreset::create(const std::string& name) {
    const auto& r = registry();
    for (const auto& p : r)
        if (name == p.name) return p.factory();
    return r[0].factory();
}
