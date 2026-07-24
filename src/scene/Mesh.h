#pragma once
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};  // w = handedness
    // Position-averaged normal used by the inverted-hull outline so hard
    // edges / UV seams don't split the hull (ZZZ-style outlines bake this
    // into vertex color or a spare UV channel; we compute it on import).
    glm::vec3 smoothNormal{0.0f, 1.0f, 0.0f};
};

class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
         std::string name);
    ~Mesh();
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;

    void draw() const;

    const std::string& name() const { return name_; }
    uint32_t indexCount() const { return indexCount_; }
    uint32_t vertexCount() const { return vertexCount_; }
    const glm::vec3& boundsMin() const { return boundsMin_; }
    const glm::vec3& boundsMax() const { return boundsMax_; }

    static std::shared_ptr<Mesh> sphere(float radius, int segments, int rings);
    static std::shared_ptr<Mesh> plane(float size);
    static std::shared_ptr<Mesh> cube(float size);

private:
    GLuint vao_ = 0, vbo_ = 0, ebo_ = 0;
    uint32_t indexCount_ = 0, vertexCount_ = 0;
    glm::vec3 boundsMin_{0.0f}, boundsMax_{0.0f};
    std::string name_;
};
