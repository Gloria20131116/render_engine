#pragma once
#include <memory>
#include <string>
#include <vector>

#include "scene/Camera.h"
#include "scene/Light.h"
#include "scene/Node.h"

struct EnvironmentSettings {
    std::string hdrPath;        // empty = procedural sky
    float intensity = 1.0f;     // IBL multiplier
    float backgroundLod = 0.0f; // blur the skybox background
    float rotationDeg = 0.0f;   // rotate environment around Y
    bool dirty = true;          // request IBL re-bake
};

class Scene {
public:
    Scene() : root(std::make_shared<Node>("Scene Root")) {}

    std::shared_ptr<Node> root;
    Camera camera;
    SunLight sun;
    std::vector<PointLight> pointLights;
    EnvironmentSettings environment;

    std::shared_ptr<Node> selected;  // editor selection

    std::string projectPath;  // current .reproj file ("" = unsaved new project)

    bool addPointLight() {
        if ((int)pointLights.size() >= kMaxPointLights) return false;
        PointLight l;
        l.name = "Point Light " + std::to_string(pointLights.size() + 1);
        float angle = pointLights.size() * 2.4f;
        l.position = {2.5f * std::cos(angle), 2.0f, 2.5f * std::sin(angle)};
        pointLights.push_back(l);
        return true;
    }
};
