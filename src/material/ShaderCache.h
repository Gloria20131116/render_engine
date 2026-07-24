#pragma once
#include <memory>
#include <string>
#include <unordered_map>

class MaterialGraph;
class Shader;
class ShaderLibrary;

// Compiles material graphs into shader programs. Generated GLSL is written to
// assets/shaders/generated/ (so it participates in the normal include/hot-
// reload machinery and can be inspected), keyed by a hash of the source so
// identical graphs share one program.
class GraphShaderCache {
public:
    void init(ShaderLibrary& shaders) { shaders_ = &shaders; }

    // Returns the compiled shader for the graph, regenerating it if the graph
    // is dirty. On failure, graph.lastError is set and the previous program
    // (if any) is kept so the scene doesn't go black while editing.
    std::shared_ptr<Shader> ensure(MaterialGraph& graph);

private:
    ShaderLibrary* shaders_ = nullptr;
    std::unordered_map<size_t, std::shared_ptr<Shader>> cache_;
};
