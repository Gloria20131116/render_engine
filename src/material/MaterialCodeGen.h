#pragma once
#include <string>
#include <vector>

class MaterialGraph;

namespace MaterialCodeGen {

// Generates the complete fragment shader for a graph (template + generated
// material function). Returns empty string and fills outError on failure
// (cycles / missing output node).
std::string generate(const MaterialGraph& graph, std::string& outError);

// Texture-bearing node ids in sampler declaration order (uGraphTex0..N).
// The renderer binds textures in exactly this order.
std::vector<int> textureNodeOrder(const MaterialGraph& graph);

}  // namespace MaterialCodeGen
