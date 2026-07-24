#include "material/ShaderCache.h"

#include <filesystem>
#include <fstream>

#include "core/Log.h"
#include "core/Paths.h"
#include "material/MaterialCodeGen.h"
#include "material/MaterialGraph.h"
#include "render/ShaderLibrary.h"

std::shared_ptr<Shader> GraphShaderCache::ensure(MaterialGraph& graph) {
    if (!graph.dirty) return graph.compiled;
    graph.dirty = false;

    std::string err;
    std::string source = MaterialCodeGen::generate(graph, err);
    if (source.empty()) {
        graph.lastError = err;
        Log::error("Material graph codegen failed: %s", err.c_str());
        return graph.compiled;  // keep last working program
    }

    size_t h = std::hash<std::string>{}(source);
    auto it = cache_.find(h);
    if (it != cache_.end()) {
        graph.compiled = it->second;
        graph.lastError = it->second->lastError();
        return graph.compiled;
    }

    // Write to disk so hot-reload of graph_common.glsl / brdf.glsl also
    // recompiles graph shaders, and so users can inspect the output.
    std::string name = "graph_" + std::to_string(h);
    std::string relPath = "generated/" + name + ".frag";
    std::filesystem::create_directories(Paths::assets() / "shaders" / "generated");
    {
        std::ofstream f(Paths::shader(relPath), std::ios::binary);
        if (!f) {
            graph.lastError = "Cannot write generated shader file";
            Log::error("GraphShaderCache: cannot write %s", Paths::shader(relPath).c_str());
            return graph.compiled;
        }
        f << source;
    }

    auto shader = shaders_->load(name, "pbr.vert", relPath);
    cache_[h] = shader;
    graph.compiled = shader;
    graph.lastError = shader ? shader->lastError() : "Shader creation failed";
    if (graph.lastError.empty())
        Log::info("Material graph compiled -> %s", relPath.c_str());
    return graph.compiled;
}
