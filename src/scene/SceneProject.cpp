#include "scene/SceneProject.h"

#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "core/Paths.h"
#include "import/ModelLoader.h"
#include "material/MaterialAsset.h"
#include "render/Renderer.h"
#include "scene/Material.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

// ---------------------------------------------------------------- helpers

static json vec3ToJson(const glm::vec3& v) { return {v.x, v.y, v.z}; }

static glm::vec3 jsonToVec3(const json& j, const glm::vec3& fallback) {
    if (!j.is_array() || j.size() < 3) return fallback;
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

static void readFloat(const json& j, const char* key, float& dst) {
    if (j.contains(key) && j[key].is_number()) dst = j[key].get<float>();
}
static void readInt(const json& j, const char* key, int& dst) {
    if (j.contains(key) && j[key].is_number()) dst = j[key].get<int>();
}
static void readBool(const json& j, const char* key, bool& dst) {
    if (j.contains(key) && j[key].is_boolean()) dst = j[key].get<bool>();
}
static void readString(const json& j, const char* key, std::string& dst) {
    if (j.contains(key) && j[key].is_string()) dst = j[key].get<std::string>();
}
static void readVec3(const json& j, const char* key, glm::vec3& dst) {
    if (j.contains(key)) dst = jsonToVec3(j[key], dst);
}

// Store paths relative to the project dir when possible (same convention as
// .mat files) so projects stay portable; fall back to the absolute path.
static std::string relPath(const std::string& path, const fs::path& baseDir) {
    if (path.empty()) return {};
    std::error_code ec;
    auto rel = fs::relative(fs::path(path), baseDir, ec);
    if (ec || rel.empty()) return path;
    return rel.generic_string();
}

static std::string resolvePath(const std::string& stored, const fs::path& baseDir) {
    if (stored.empty()) return {};
    fs::path p(stored);
    if (p.is_absolute()) return p.string();
    std::error_code ec;
    auto canonical = fs::weakly_canonical(baseDir / p, ec);
    return ec ? (baseDir / p).string() : canonical.string();
}

static fs::path lastProjectMarker() { return Paths::assets() / "last_project.txt"; }

// ---------------------------------------------------------------- node save

static void nodeToJson(const Node& n, json& j, const fs::path& baseDir) {
    j["name"] = n.name;
    j["visible"] = n.visible;
    j["position"] = vec3ToJson(n.position);
    j["rotation"] = vec3ToJson(n.rotationEuler);
    j["scale"] = vec3ToJson(n.scale);

    if (!n.sourceModelPath.empty()) j["model"] = relPath(n.sourceModelPath, baseDir);
    if (n.primitive != PrimitiveKind::None) {
        j["primitive"] = (int)n.primitive;
        j["primitiveParams"] = vec3ToJson(n.primitiveParams);
    }

    if (n.material) {
        json m;
        MaterialAsset::toJson(*n.material, m, baseDir);
        j["material"] = m;
    }

    if (!n.children.empty()) {
        json arr = json::array();
        for (const auto& c : n.children) {
            json cj;
            nodeToJson(*c, cj, baseDir);
            arr.push_back(std::move(cj));
        }
        j["children"] = arr;
    }
}

// ---------------------------------------------------------------- node load

static void applyNodeState(const json& j, Node& n, const fs::path& baseDir);

// Match saved children to a freshly imported subtree: prefer same index with
// same name, else search by name (model file may have changed since saving).
static void applyChildren(const json& j, Node& n, const fs::path& baseDir) {
    if (!j.contains("children") || !j["children"].is_array()) return;
    const auto& arr = j["children"];
    for (size_t i = 0; i < arr.size(); ++i) {
        const json& cj = arr[i];
        std::string name;
        readString(cj, "name", name);

        Node* target = nullptr;
        if (i < n.children.size() && n.children[i]->name == name)
            target = n.children[i].get();
        if (!target) {
            for (auto& c : n.children)
                if (c->name == name) { target = c.get(); break; }
        }
        if (!target && i < n.children.size()) target = n.children[i].get();
        if (target) applyNodeState(cj, *target, baseDir);
    }
}

static void applyNodeState(const json& j, Node& n, const fs::path& baseDir) {
    readString(j, "name", n.name);
    readBool(j, "visible", n.visible);
    readVec3(j, "position", n.position);
    readVec3(j, "rotation", n.rotationEuler);
    readVec3(j, "scale", n.scale);

    if (j.contains("material") && j["material"].is_object()) {
        if (!n.material) n.material = std::make_shared<Material>();
        MaterialAsset::fromJson(j["material"], *n.material, baseDir);
    }
    applyChildren(j, n, baseDir);
}

static std::shared_ptr<Mesh> buildPrimitive(PrimitiveKind kind, const glm::vec3& p) {
    switch (kind) {
        case PrimitiveKind::Sphere: {
            float radius = p.x > 0.0f ? p.x : 0.5f;
            int segments = p.y >= 3.0f ? (int)p.y : 48;
            int rings = p.z >= 3.0f ? (int)p.z : 32;
            return Mesh::sphere(radius, segments, rings);
        }
        case PrimitiveKind::Cube:
            return Mesh::cube(p.x > 0.0f ? p.x : 1.0f);
        case PrimitiveKind::Plane:
            return Mesh::plane(p.x > 0.0f ? p.x : 10.0f);
        default:
            return nullptr;
    }
}

static std::shared_ptr<Node> nodeFromJson(const json& j, const fs::path& baseDir) {
    std::string modelRel;
    readString(j, "model", modelRel);

    if (!modelRel.empty()) {
        std::string modelPath = resolvePath(modelRel, baseDir);
        std::shared_ptr<Node> node;
        if (fs::exists(modelPath)) node = ModelLoader::load(modelPath);
        if (!node) {
            Log::error("Project: cannot re-import model '%s' (missing or failed); "
                       "keeping an empty placeholder node", modelPath.c_str());
            node = std::make_shared<Node>("(missing) " + fs::path(modelPath).filename().string());
        }
        node->sourceModelPath = modelPath;
        // Re-apply saved transforms/materials over the imported subtree.
        applyNodeState(j, *node, baseDir);
        return node;
    }

    std::string name = "Node";
    readString(j, "name", name);
    auto node = std::make_shared<Node>(name);
    readBool(j, "visible", node->visible);
    readVec3(j, "position", node->position);
    readVec3(j, "rotation", node->rotationEuler);
    readVec3(j, "scale", node->scale);

    int prim = 0;
    readInt(j, "primitive", prim);
    if (prim != 0) {
        node->primitive = (PrimitiveKind)prim;
        readVec3(j, "primitiveParams", node->primitiveParams);
        node->mesh = buildPrimitive(node->primitive, node->primitiveParams);
    }

    if (j.contains("material") && j["material"].is_object()) {
        node->material = std::make_shared<Material>();
        MaterialAsset::fromJson(j["material"], *node->material, baseDir);
    }

    if (j.contains("children") && j["children"].is_array()) {
        for (const auto& cj : j["children"]) {
            auto child = nodeFromJson(cj, baseDir);
            if (child) node->addChild(child);
        }
    }
    return node;
}

// ---------------------------------------------------------------- settings

static const char* tonemapStr(TonemapMode m) {
    switch (m) {
        case TonemapMode::ACES:     return "ACES";
        case TonemapMode::Reinhard: return "Reinhard";
        case TonemapMode::None:     return "None";
    }
    return "ACES";
}

static TonemapMode parseTonemap(const std::string& s) {
    if (s == "Reinhard") return TonemapMode::Reinhard;
    if (s == "None")     return TonemapMode::None;
    return TonemapMode::ACES;
}

// ---------------------------------------------------------------- public API

bool SceneProject::save(Scene& scene, Renderer& renderer, const std::string& path) {
    fs::path projPath(path);
    fs::path baseDir = projPath.parent_path();
    if (!baseDir.empty()) {
        std::error_code ec;
        fs::create_directories(baseDir, ec);
    }

    json j;
    j["version"] = 1;

    // Camera
    json cam;
    cam["target"] = vec3ToJson(scene.camera.target);
    cam["distance"] = scene.camera.distance;
    cam["yaw"] = scene.camera.yaw;
    cam["pitch"] = scene.camera.pitch;
    cam["fovY"] = scene.camera.fovY;
    cam["nearZ"] = scene.camera.nearZ;
    cam["farZ"] = scene.camera.farZ;
    j["camera"] = cam;

    // Sun
    json sun;
    sun["azimuth"] = scene.sun.azimuth;
    sun["elevation"] = scene.sun.elevation;
    sun["color"] = vec3ToJson(scene.sun.color);
    sun["intensity"] = scene.sun.intensity;
    sun["castShadows"] = scene.sun.castShadows;
    sun["shadowOrthoSize"] = scene.sun.shadowOrthoSize;
    sun["shadowBias"] = scene.sun.shadowBias;
    j["sun"] = sun;

    // Point lights
    json lights = json::array();
    for (const auto& l : scene.pointLights) {
        json lj;
        lj["name"] = l.name;
        lj["enabled"] = l.enabled;
        lj["position"] = vec3ToJson(l.position);
        lj["color"] = vec3ToJson(l.color);
        lj["intensity"] = l.intensity;
        lj["radius"] = l.radius;
        lights.push_back(std::move(lj));
    }
    j["pointLights"] = lights;

    // Environment
    json env;
    env["hdrPath"] = relPath(scene.environment.hdrPath, baseDir);
    env["intensity"] = scene.environment.intensity;
    env["backgroundLod"] = scene.environment.backgroundLod;
    env["rotationDeg"] = scene.environment.rotationDeg;
    j["environment"] = env;

    // Renderer / post-processing
    json rj;
    rj["exposure"] = renderer.settings.exposure;
    rj["tonemap"] = tonemapStr(renderer.settings.tonemap);
    rj["wireframe"] = renderer.settings.wireframe;
    rj["msaaSamples"] = renderer.settings.msaaSamples;
    rj["outlineEnabled"] = renderer.settings.outlineEnabled;
    json bj;
    bj["enabled"] = renderer.bloom().settings.enabled;
    bj["threshold"] = renderer.bloom().settings.threshold;
    bj["kneeSoftness"] = renderer.bloom().settings.kneeSoftness;
    bj["intensity"] = renderer.bloom().settings.intensity;
    bj["radius"] = renderer.bloom().settings.radius;
    rj["bloom"] = bj;
    j["renderer"] = rj;

    // Node tree (children of the scene root)
    json nodes = json::array();
    for (const auto& c : scene.root->children) {
        json nj;
        nodeToJson(*c, nj, baseDir);
        nodes.push_back(std::move(nj));
    }
    j["nodes"] = nodes;

    std::ofstream ofs(projPath);
    if (!ofs) {
        Log::error("SceneProject::save – cannot open '%s' for writing", path.c_str());
        return false;
    }
    ofs << j.dump(4);

    scene.projectPath = path;
    {
        std::ofstream marker(lastProjectMarker());
        if (marker) marker << path;
    }
    Log::info("Project saved: %s", path.c_str());
    return true;
}

bool SceneProject::load(const std::string& path, Scene& scene, Renderer& renderer) {
    std::ifstream ifs(path);
    if (!ifs) {
        Log::error("SceneProject::load – cannot open '%s'", path.c_str());
        return false;
    }

    json j;
    try {
        ifs >> j;
    } catch (const json::parse_error& e) {
        Log::error("SceneProject::load – JSON parse error in '%s': %s", path.c_str(), e.what());
        return false;
    }

    fs::path baseDir = fs::path(path).parent_path();

    // Reset the current scene
    scene.selected = nullptr;
    scene.root->children.clear();
    scene.pointLights.clear();

    // Camera
    if (j.contains("camera")) {
        const json& cam = j["camera"];
        readVec3(cam, "target", scene.camera.target);
        readFloat(cam, "distance", scene.camera.distance);
        readFloat(cam, "yaw", scene.camera.yaw);
        readFloat(cam, "pitch", scene.camera.pitch);
        readFloat(cam, "fovY", scene.camera.fovY);
        readFloat(cam, "nearZ", scene.camera.nearZ);
        readFloat(cam, "farZ", scene.camera.farZ);
    }

    // Sun
    if (j.contains("sun")) {
        const json& sun = j["sun"];
        readFloat(sun, "azimuth", scene.sun.azimuth);
        readFloat(sun, "elevation", scene.sun.elevation);
        readVec3(sun, "color", scene.sun.color);
        readFloat(sun, "intensity", scene.sun.intensity);
        readBool(sun, "castShadows", scene.sun.castShadows);
        readFloat(sun, "shadowOrthoSize", scene.sun.shadowOrthoSize);
        readFloat(sun, "shadowBias", scene.sun.shadowBias);
    }

    // Point lights
    if (j.contains("pointLights") && j["pointLights"].is_array()) {
        for (const auto& lj : j["pointLights"]) {
            if ((int)scene.pointLights.size() >= kMaxPointLights) break;
            PointLight l;
            readString(lj, "name", l.name);
            readBool(lj, "enabled", l.enabled);
            readVec3(lj, "position", l.position);
            readVec3(lj, "color", l.color);
            readFloat(lj, "intensity", l.intensity);
            readFloat(lj, "radius", l.radius);
            scene.pointLights.push_back(l);
        }
    }

    // Environment
    if (j.contains("environment")) {
        const json& env = j["environment"];
        std::string hdr;
        readString(env, "hdrPath", hdr);
        std::string resolved = resolvePath(hdr, baseDir);
        if (!resolved.empty() && !fs::exists(resolved)) {
            Log::warn("Project: HDR '%s' not found, falling back to procedural sky",
                      resolved.c_str());
            resolved.clear();
        }
        scene.environment.hdrPath = resolved;
        readFloat(env, "intensity", scene.environment.intensity);
        readFloat(env, "backgroundLod", scene.environment.backgroundLod);
        readFloat(env, "rotationDeg", scene.environment.rotationDeg);
    }
    scene.environment.dirty = true;  // re-bake IBL

    // Renderer / post-processing
    if (j.contains("renderer")) {
        const json& rj = j["renderer"];
        readFloat(rj, "exposure", renderer.settings.exposure);
        std::string tm;
        readString(rj, "tonemap", tm);
        if (!tm.empty()) renderer.settings.tonemap = parseTonemap(tm);
        readBool(rj, "wireframe", renderer.settings.wireframe);
        readInt(rj, "msaaSamples", renderer.settings.msaaSamples);
        readBool(rj, "outlineEnabled", renderer.settings.outlineEnabled);
        if (rj.contains("bloom")) {
            const json& bj = rj["bloom"];
            readBool(bj, "enabled", renderer.bloom().settings.enabled);
            readFloat(bj, "threshold", renderer.bloom().settings.threshold);
            readFloat(bj, "kneeSoftness", renderer.bloom().settings.kneeSoftness);
            readFloat(bj, "intensity", renderer.bloom().settings.intensity);
            readFloat(bj, "radius", renderer.bloom().settings.radius);
        }
    }

    // Node tree
    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& nj : j["nodes"]) {
            auto node = nodeFromJson(nj, baseDir);
            if (node) scene.root->addChild(node);
        }
    }

    scene.projectPath = path;
    {
        std::ofstream marker(lastProjectMarker());
        if (marker) marker << path;
    }
    Log::info("Project loaded: %s", path.c_str());
    return true;
}

std::string SceneProject::readLastProjectPath() {
    std::ifstream ifs(lastProjectMarker());
    if (!ifs) return {};
    std::string line;
    std::getline(ifs, line);
    return line;
}

void SceneProject::clearLastProjectPath() {
    std::error_code ec;
    fs::remove(lastProjectMarker(), ec);
}
