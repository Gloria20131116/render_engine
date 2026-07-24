#include "scene/Mesh.h"

#include <glm/gtc/constants.hpp>

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices,
           std::string name)
    : name_(std::move(name)) {
    vertexCount_ = (uint32_t)vertices.size();
    indexCount_ = (uint32_t)indices.size();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(),
                 GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(),
                 GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, tangent));
    glBindVertexArray(0);
}

Mesh::~Mesh() {
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (ebo_) glDeleteBuffers(1, &ebo_);
}

void Mesh::draw() const {
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

std::shared_ptr<Mesh> Mesh::sphere(float radius, int segments, int rings) {
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    const float PI = glm::pi<float>();
    for (int y = 0; y <= rings; ++y) {
        float v = (float)y / rings;
        float phi = v * PI;
        for (int x = 0; x <= segments; ++x) {
            float u = (float)x / segments;
            float theta = u * 2.0f * PI;
            glm::vec3 n(std::sin(phi) * std::cos(theta), std::cos(phi),
                        std::sin(phi) * std::sin(theta));
            Vertex vert;
            vert.position = n * radius;
            vert.normal = n;
            vert.uv = {u, 1.0f - v};
            glm::vec3 t(-std::sin(theta), 0.0f, std::cos(theta));
            vert.tangent = glm::vec4(t, 1.0f);
            verts.push_back(vert);
        }
    }
    for (int y = 0; y < rings; ++y) {
        for (int x = 0; x < segments; ++x) {
            uint32_t a = y * (segments + 1) + x;
            uint32_t b = a + segments + 1;
            // CCW seen from outside
            idx.insert(idx.end(), {a, a + 1, b, b, a + 1, b + 1});
        }
    }
    return std::make_shared<Mesh>(verts, idx, "Sphere");
}

std::shared_ptr<Mesh> Mesh::plane(float size) {
    float s = size * 0.5f;
    std::vector<Vertex> verts(4);
    verts[0].position = {-s, 0, -s};
    verts[1].position = {s, 0, -s};
    verts[2].position = {s, 0, s};
    verts[3].position = {-s, 0, s};
    for (int i = 0; i < 4; ++i) {
        verts[i].normal = {0, 1, 0};
        verts[i].tangent = {1, 0, 0, 1};
    }
    float uvScale = size * 0.25f;
    verts[0].uv = {0, 0};
    verts[1].uv = {uvScale, 0};
    verts[2].uv = {uvScale, uvScale};
    verts[3].uv = {0, uvScale};
    std::vector<uint32_t> idx = {0, 2, 1, 0, 3, 2};
    return std::make_shared<Mesh>(verts, idx, "Plane");
}

std::shared_ptr<Mesh> Mesh::cube(float size) {
    float s = size * 0.5f;
    std::vector<Vertex> verts;
    std::vector<uint32_t> idx;
    const glm::vec3 normals[6] = {{0, 0, 1}, {0, 0, -1}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
    for (int f = 0; f < 6; ++f) {
        glm::vec3 n = normals[f];
        glm::vec3 up = std::abs(n.y) > 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        glm::vec3 right = glm::normalize(glm::cross(up, n));
        up = glm::cross(n, right);
        uint32_t base = (uint32_t)verts.size();
        const glm::vec2 corners[4] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (int c = 0; c < 4; ++c) {
            Vertex v;
            v.position = (n + right * corners[c].x + up * corners[c].y) * s;
            v.normal = n;
            v.uv = {(corners[c].x + 1) * 0.5f, (corners[c].y + 1) * 0.5f};
            v.tangent = glm::vec4(right, 1.0f);
            verts.push_back(v);
        }
        idx.insert(idx.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return std::make_shared<Mesh>(verts, idx, "Cube");
}
