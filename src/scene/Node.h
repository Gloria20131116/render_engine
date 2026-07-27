#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <memory>
#include <string>
#include <vector>

class Mesh;
struct Material;

// How this node's mesh was created, so a project file can rebuild it.
enum class PrimitiveKind : int { None = 0, Sphere = 1, Cube = 2, Plane = 3 };

// Scene graph node. Owns children; may reference a mesh + material.
class Node : public std::enable_shared_from_this<Node> {
public:
    explicit Node(std::string name) : name(std::move(name)) {}

    std::string name;
    bool visible = true;

    // ---- Serialization hints (project save/load) ----
    std::string sourceModelPath;              // set on the root of an imported model subtree
    PrimitiveKind primitive = PrimitiveKind::None;
    glm::vec3 primitiveParams{0.0f};          // sphere: radius/segments/rings; cube/plane: size

    glm::vec3 position{0.0f};
    glm::vec3 rotationEuler{0.0f};  // degrees
    glm::vec3 scale{1.0f};

    std::shared_ptr<Mesh> mesh;
    std::shared_ptr<Material> material;

    Node* parent = nullptr;
    std::vector<std::shared_ptr<Node>> children;

    std::shared_ptr<Node> addChild(const std::string& childName) {
        auto child = std::make_shared<Node>(childName);
        child->parent = this;
        children.push_back(child);
        return child;
    }

    void addChild(const std::shared_ptr<Node>& child) {
        child->parent = this;
        children.push_back(child);
    }

    void removeChild(const Node* child) {
        for (auto it = children.begin(); it != children.end(); ++it) {
            if (it->get() == child) {
                children.erase(it);
                return;
            }
        }
    }

    glm::mat4 localMatrix() const {
        glm::mat4 m(1.0f);
        m = glm::translate(m, position);
        m *= glm::eulerAngleYXZ(glm::radians(rotationEuler.y), glm::radians(rotationEuler.x),
                                glm::radians(rotationEuler.z));
        m = glm::scale(m, scale);
        return m;
    }

    glm::mat4 worldMatrix() const {
        return parent ? parent->worldMatrix() * localMatrix() : localMatrix();
    }

    template <typename F>
    void traverse(F&& fn, const glm::mat4& parentWorld = glm::mat4(1.0f)) {
        if (!visible) return;
        glm::mat4 world = parentWorld * localMatrix();
        fn(*this, world);
        for (auto& c : children) c->traverse(fn, world);
    }
};
