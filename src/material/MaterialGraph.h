#pragma once
#include <glm/glm.hpp>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>

class Texture;
class Shader;

// UE-style material node graph. Nodes produce vec4 values (everything is
// vec4 internally, like UE's implicit float promotion); the Output node's
// input pins feed the lighting template (see MaterialCodeGen).

enum class PinType : int { Float = 0, Vec2, Vec3, Vec4, Texture2D };

struct Pin {
    int id = 0;
    std::string name;
    PinType type = PinType::Float;
    glm::vec4 defaultValue{0.0f};
    std::string defaultExpr;  // GLSL used when unconnected (overrides defaultValue)
};

struct GraphNode {
    int id = 0;
    std::string type;  // registry key, e.g. "Add", "TextureSample", "Output"
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
    glm::vec2 position{0.0f};
    bool posDirty = true;  // push `position` into imnodes on next draw

    // Payload (meaning depends on the node type)
    glm::vec4 value{0.0f};             // editable constant -> uniform uN<id>
    std::shared_ptr<Texture> texture;  // for texture-bearing nodes
    std::string texturePath;
    bool srgb = true;
    std::string customCode;  // body of the Custom node's generated function
};

struct Link {
    int id = 0;
    int fromPin = 0;  // output pin id
    int toPin = 0;    // input pin id
};

// ---- Static node type registry (shared by editor + codegen) ----

struct PinDef {
    const char* name;
    PinType type;
    glm::vec4 def{0.0f};
    const char* defExpr = nullptr;  // GLSL fallback when unconnected
};

struct NodeTypeDef {
    const char* type;
    const char* category;  // menu grouping: Constant/Math/Vector/Texture/UV/Utility/Noise/Toon
    std::vector<PinDef> inputs;
    std::vector<PinDef> outputs;
    // One vec4-valued GLSL expression per output pin.
    // Placeholders: $0..$15 = resolved inputs (vec4), @VAL = constant uniform,
    // @TEX = this node's sampler.
    std::vector<const char*> exprs;
    bool hasValue = false;    // node.value editable -> uniform
    bool isColor = false;     // edit value with a color picker
    bool hasTexture = false;  // node owns a sampler2D
    int valueComponents = 1;  // editable components of `value`
    bool hasCode = false;     // UE-style Custom node: user-written function body
    const char* defaultCode = nullptr;
};

class MaterialGraph {
public:
    MaterialGraph();

    std::vector<GraphNode> nodes;
    std::vector<Link> links;
    int nextId = 1;

    // Compile state (managed by GraphShaderCache / NodeEditor)
    bool dirty = true;                 // structure changed -> needs regenerate
    std::shared_ptr<Shader> compiled;  // last successfully created program
    std::string lastError;             // codegen or compile error, empty = OK

    int outputNodeId() const { return outputNodeId_; }
    GraphNode* outputNode() { return findNode(outputNodeId_); }

    GraphNode& addNode(const std::string& type, glm::vec2 pos);
    void removeNode(int nodeId);
    // Connects an output pin to an input pin (order-agnostic). Replaces any
    // existing link on the input. Returns false for invalid pairs or cycles.
    bool addLink(int pinA, int pinB);
    void removeLink(int linkId);

    GraphNode* findNode(int id);
    const GraphNode* findNode(int id) const;
    const GraphNode* findPin(int pinId, bool* outIsInput, int* outPinIndex) const;
    const Link* linkToInput(int inputPinId) const;

    static const std::vector<NodeTypeDef>& nodeTypes();
    static const NodeTypeDef* typeDef(const std::string& type);

    void toJson(nlohmann::json& j) const;
    static std::shared_ptr<MaterialGraph> fromJson(const nlohmann::json& j);

private:
    bool reachesNode(int startNode, int targetNode) const;
    int outputNodeId_ = 0;
};
