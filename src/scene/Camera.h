#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Orbit camera for lookdev: yaw/pitch around a target point.
class Camera {
public:
    glm::vec3 target{0.0f, 1.0f, 0.0f};
    float distance = 4.0f;
    float yaw = -90.0f;    // degrees
    float pitch = 10.0f;   // degrees
    float fovY = 45.0f;
    float nearZ = 0.05f;
    float farZ = 200.0f;

    glm::vec3 position() const {
        float cy = std::cos(glm::radians(yaw)), sy = std::sin(glm::radians(yaw));
        float cp = std::cos(glm::radians(pitch)), sp = std::sin(glm::radians(pitch));
        // pitch > 0 places the camera above the target looking down
        glm::vec3 dir(cy * cp, -sp, sy * cp);
        return target - dir * distance;
    }

    glm::mat4 view() const { return glm::lookAt(position(), target, {0, 1, 0}); }

    glm::mat4 projection(float aspect) const {
        return glm::perspective(glm::radians(fovY), aspect, nearZ, farZ);
    }

    void orbit(float dx, float dy) {
        yaw += dx * 0.3f;
        pitch = glm::clamp(pitch + dy * 0.3f, -89.0f, 89.0f);
    }

    void pan(float dx, float dy) {
        glm::mat4 v = view();
        glm::vec3 right(v[0][0], v[1][0], v[2][0]);
        glm::vec3 up(v[0][1], v[1][1], v[2][1]);
        float speed = distance * 0.0015f;
        target += (-right * dx + up * dy) * speed;
    }

    void zoom(float delta) {
        distance = glm::clamp(distance * (1.0f - delta * 0.1f), 0.05f, 1000.0f);
    }
};
