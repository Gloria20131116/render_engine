#pragma once
#include <glm/glm.hpp>
#include <string>

// Directional sun light. Direction derived from azimuth/elevation for easy UI editing.
struct SunLight {
    float azimuth = 45.0f;    // degrees around Y
    float elevation = 40.0f;  // degrees above horizon
    glm::vec3 color{1.0f, 0.96f, 0.9f};
    float intensity = 4.0f;
    bool castShadows = true;
    float shadowOrthoSize = 8.0f;
    float shadowBias = 0.0015f;

    glm::vec3 direction() const {  // points FROM sun TOWARDS scene
        float az = glm::radians(azimuth), el = glm::radians(elevation);
        return -glm::normalize(glm::vec3(std::cos(el) * std::cos(az), std::sin(el),
                                         std::cos(el) * std::sin(az)));
    }
};

struct PointLight {
    std::string name = "Point Light";
    bool enabled = true;
    glm::vec3 position{0.0f, 2.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity = 10.0f;  // luminous-ish, divided by squared distance
    float radius = 15.0f;     // influence cutoff
};

constexpr int kMaxPointLights = 5;
