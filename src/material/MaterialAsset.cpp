#include "material/MaterialAsset.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "material/MaterialGraph.h"
#include "render/Texture.h"
#include "scene/Material.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---- glm helpers ----

static json vec3ToJson(const glm::vec3& v) { return {v.x, v.y, v.z}; }

static glm::vec3 jsonToVec3(const json& j, const glm::vec3& fallback = glm::vec3(0.0f)) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

// ---- enum string tables ----

static const char* shadingModelStr(ShadingModel m) {
    switch (m) {
        case ShadingModel::PBR:  return "PBR";
        case ShadingModel::Toon: return "Toon";
    }
    return "PBR";
}

static ShadingModel parseShadingModel(const std::string& s) {
    if (s == "Toon") return ShadingModel::Toon;
    return ShadingModel::PBR;
}

static const char* ndfStr(NDFType t) {
    switch (t) {
        case NDFType::GGX:       return "GGX";
        case NDFType::Beckmann:  return "Beckmann";
        case NDFType::BlinnPhong:return "BlinnPhong";
    }
    return "GGX";
}

static NDFType parseNDF(const std::string& s) {
    if (s == "Beckmann")  return NDFType::Beckmann;
    if (s == "BlinnPhong") return NDFType::BlinnPhong;
    return NDFType::GGX;
}

static const char* geomStr(GeomType t) {
    switch (t) {
        case GeomType::SmithGGX:       return "SmithGGX";
        case GeomType::SmithSchlickGGX:return "SmithSchlickGGX";
        case GeomType::Implicit:       return "Implicit";
    }
    return "SmithGGX";
}

static GeomType parseGeom(const std::string& s) {
    if (s == "SmithSchlickGGX") return GeomType::SmithSchlickGGX;
    if (s == "Implicit")        return GeomType::Implicit;
    return GeomType::SmithGGX;
}

static const char* fresnelStr(FresnelType t) {
    switch (t) {
        case FresnelType::Schlick:          return "Schlick";
        case FresnelType::SchlickRoughness: return "SchlickRoughness";
        case FresnelType::None:             return "None";
    }
    return "Schlick";
}

static FresnelType parseFresnel(const std::string& s) {
    if (s == "SchlickRoughness") return FresnelType::SchlickRoughness;
    if (s == "None")             return FresnelType::None;
    return FresnelType::Schlick;
}

// ---- texture path helpers ----

static std::string textureRelPath(const std::shared_ptr<Texture>& tex, const fs::path& matDir) {
    if (!tex) return "";
    const auto& src = tex->sourcePath();
    if (src.empty()) return "";
    std::error_code ec;
    auto rel = fs::relative(fs::path(src), matDir, ec);
    if (ec) return src;
    return rel.generic_string();
}

static std::shared_ptr<Texture> loadTexture(const std::string& relPath, const fs::path& matDir,
                                            bool srgb) {
    if (relPath.empty()) return nullptr;
    fs::path resolved = matDir / fs::path(relPath);
    std::error_code ec;
    auto canonical = fs::canonical(resolved, ec);
    std::string loadPath = ec ? resolved.string() : canonical.string();
    return Texture::load2D(loadPath, srgb);
}

// ---- public API ----

bool MaterialAsset::save(const Material& mat, const std::string& path) {
    fs::path matPath(path);
    fs::path matDir = matPath.parent_path();
    if (!matDir.empty()) {
        std::error_code ec;
        fs::create_directories(matDir, ec);
    }

    json j;
    j["name"]  = mat.name;
    j["model"] = shadingModelStr(mat.model);

    // Surface
    j["baseColor"]         = vec3ToJson(mat.baseColor);
    j["metallic"]          = mat.metallic;
    j["roughness"]         = mat.roughness;
    j["ao"]                = mat.ao;
    j["emissive"]          = vec3ToJson(mat.emissive);
    j["emissiveIntensity"] = mat.emissiveIntensity;
    j["normalStrength"]    = mat.normalStrength;
    j["specularF0"]        = mat.specularF0;
    j["iblIntensity"]      = mat.iblIntensity;
    j["alphaCutoff"]       = mat.alphaCutoff;

    // BRDF
    j["ndf"]                = ndfStr(mat.ndf);
    j["geom"]               = geomStr(mat.geom);
    j["fresnel"]            = fresnelStr(mat.fresnel);
    j["specularTint"]       = mat.specularTint;
    j["energyCompensation"] = mat.energyCompensation;

    // Toon
    j["shadowColor"]        = vec3ToJson(mat.shadowColor);
    j["shadowThreshold"]    = mat.shadowThreshold;
    j["shadowSoftness"]     = mat.shadowSoftness;
    j["rampShift"]          = mat.rampShift;
    j["rimColor"]           = vec3ToJson(mat.rimColor);
    j["rimWidth"]           = mat.rimWidth;
    j["rimIntensity"]       = mat.rimIntensity;
    j["toonSpecSize"]       = mat.toonSpecSize;
    j["toonSpecIntensity"]  = mat.toonSpecIntensity;
    j["toonSpecColor"]      = vec3ToJson(mat.toonSpecColor);

    // Outline
    j["outline"]              = mat.outline;
    j["outlineWidthPx"]       = mat.outlineWidthPx;
    j["outlineMaxWorldWidth"] = mat.outlineMaxWorldWidth;
    j["outlineZOffset"]       = mat.outlineZOffset;
    j["outlineColor"]         = vec3ToJson(mat.outlineColor);
    j["outlineFromBaseColor"] = mat.outlineFromBaseColor;
    j["outlineColorScale"]    = mat.outlineColorScale;

    // Textures (relative paths)
    json textures;
    textures["albedo"]   = textureRelPath(mat.albedoMap,    matDir);
    textures["normal"]   = textureRelPath(mat.normalMap,    matDir);
    textures["metallic"] = textureRelPath(mat.metallicMap,  matDir);
    textures["roughness"]= textureRelPath(mat.roughnessMap, matDir);
    textures["ao"]       = textureRelPath(mat.aoMap,        matDir);
    textures["emissive"] = textureRelPath(mat.emissiveMap,  matDir);
    textures["ramp"]     = textureRelPath(mat.rampMap,      matDir);
    j["textures"] = textures;

    j["doubleSided"] = mat.doubleSided;
    j["flipNormals"] = mat.flipNormals;

    if (mat.graph) {
        json g;
        mat.graph->toJson(g);
        j["graph"] = g;
    }

    std::ofstream ofs(matPath);
    if (!ofs) {
        Log::error("MaterialAsset::save – cannot open '%s' for writing", path.c_str());
        return false;
    }
    ofs << j.dump(4);
    Log::info("Saved material '%s' -> %s", mat.name.c_str(), path.c_str());
    return true;
}

bool MaterialAsset::load(const std::string& path, Material& outMat) {
    std::ifstream ifs(path);
    if (!ifs) {
        Log::error("MaterialAsset::load – cannot open '%s'", path.c_str());
        return false;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        Log::error("MaterialAsset::load – JSON parse error in '%s': %s", path.c_str(), e.what());
        return false;
    }

    fs::path matDir = fs::path(path).parent_path();

    auto str  = [&](const char* key, std::string& dst)  { if (j.contains(key) && j[key].is_string())  dst = j[key].get<std::string>(); };
    auto flt  = [&](const char* key, float& dst)         { if (j.contains(key) && j[key].is_number())  dst = j[key].get<float>(); };
    auto bl   = [&](const char* key, bool& dst)          { if (j.contains(key) && j[key].is_boolean()) dst = j[key].get<bool>(); };
    auto v3   = [&](const char* key, glm::vec3& dst)     { if (j.contains(key)) dst = jsonToVec3(j[key], dst); };

    str("name", outMat.name);

    if (j.contains("model") && j["model"].is_string())
        outMat.model = parseShadingModel(j["model"].get<std::string>());

    // Surface
    v3 ("baseColor",   outMat.baseColor);
    flt("metallic",    outMat.metallic);
    flt("roughness",   outMat.roughness);
    flt("ao",          outMat.ao);
    v3 ("emissive",    outMat.emissive);
    flt("emissiveIntensity", outMat.emissiveIntensity);
    flt("normalStrength",    outMat.normalStrength);
    flt("specularF0",        outMat.specularF0);
    flt("iblIntensity",      outMat.iblIntensity);
    flt("alphaCutoff",       outMat.alphaCutoff);

    // BRDF
    if (j.contains("ndf")     && j["ndf"].is_string())     outMat.ndf     = parseNDF(j["ndf"].get<std::string>());
    if (j.contains("geom")    && j["geom"].is_string())    outMat.geom    = parseGeom(j["geom"].get<std::string>());
    if (j.contains("fresnel") && j["fresnel"].is_string()) outMat.fresnel = parseFresnel(j["fresnel"].get<std::string>());
    flt("specularTint",       outMat.specularTint);
    flt("energyCompensation", outMat.energyCompensation);

    // Toon
    v3 ("shadowColor",    outMat.shadowColor);
    flt("shadowThreshold",outMat.shadowThreshold);
    flt("shadowSoftness", outMat.shadowSoftness);
    flt("rampShift",      outMat.rampShift);
    v3 ("rimColor",       outMat.rimColor);
    flt("rimWidth",       outMat.rimWidth);
    flt("rimIntensity",   outMat.rimIntensity);
    flt("toonSpecSize",       outMat.toonSpecSize);
    flt("toonSpecIntensity",  outMat.toonSpecIntensity);
    v3 ("toonSpecColor",      outMat.toonSpecColor);

    // Outline
    bl ("outline",              outMat.outline);
    flt("outlineWidthPx",       outMat.outlineWidthPx);
    flt("outlineMaxWorldWidth", outMat.outlineMaxWorldWidth);
    flt("outlineZOffset",       outMat.outlineZOffset);
    v3 ("outlineColor",         outMat.outlineColor);
    bl ("outlineFromBaseColor", outMat.outlineFromBaseColor);
    flt("outlineColorScale",    outMat.outlineColorScale);

    // Textures
    if (j.contains("textures") && j["textures"].is_object()) {
        const auto& tex = j["textures"];
        auto loadSlot = [&](const char* key, std::shared_ptr<Texture>& dst, bool srgb) {
            if (tex.contains(key) && tex[key].is_string()) {
                std::string rel = tex[key].get<std::string>();
                dst = loadTexture(rel, matDir, srgb);
            }
        };
        loadSlot("albedo",    outMat.albedoMap,    true);
        loadSlot("normal",    outMat.normalMap,    false);
        loadSlot("metallic",  outMat.metallicMap,  false);
        loadSlot("roughness", outMat.roughnessMap, false);
        loadSlot("ao",        outMat.aoMap,        false);
        loadSlot("emissive",  outMat.emissiveMap,  true);
        loadSlot("ramp",      outMat.rampMap,      false);
    }

    bl("doubleSided", outMat.doubleSided);
    bl("flipNormals", outMat.flipNormals);

    outMat.graph = j.contains("graph") && j["graph"].is_object()
                       ? MaterialGraph::fromJson(j["graph"])
                       : nullptr;

    outMat.assetPath = path;
    Log::info("Loaded material '%s' from %s", outMat.name.c_str(), path.c_str());
    return true;
}
