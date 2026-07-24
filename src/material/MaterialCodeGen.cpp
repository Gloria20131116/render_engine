#include "material/MaterialCodeGen.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>
#include <sstream>
#include <unordered_map>

#include "material/MaterialGraph.h"

namespace {

std::string fmtFloat(float v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    return buf;
}

std::string vec4Literal(const glm::vec4& v) {
    return "vec4(" + fmtFloat(v.x) + ", " + fmtFloat(v.y) + ", " + fmtFloat(v.z) + ", " +
           fmtFloat(v.w) + ")";
}

std::string outputVar(int nodeId, int pinIndex) {
    return "n" + std::to_string(nodeId) + "_" + std::to_string(pinIndex);
}

struct GenContext {
    const MaterialGraph* graph = nullptr;
    std::unordered_map<int, int> samplerIndex;  // texture node id -> uGraphTexN
};

// Resolve the vec4 expression feeding input pin `pin` of `node`.
std::string resolveInput(const GenContext& ctx, const Pin& pin) {
    const Link* l = ctx.graph->linkToInput(pin.id);
    if (l) {
        bool isInput = false;
        int idx = 0;
        const GraphNode* src = ctx.graph->findPin(l->fromPin, &isInput, &idx);
        if (src && !isInput) return outputVar(src->id, idx);
    }
    if (!pin.defaultExpr.empty()) return pin.defaultExpr;
    return vec4Literal(pin.defaultValue);
}

// Substitute $0..$15 / @VAL / @TEX in a node expression template.
std::string substitute(const GenContext& ctx, const GraphNode& node, const std::string& tmpl,
                       const std::vector<std::string>& inputExprs) {
    std::string out;
    out.reserve(tmpl.size() * 2);
    for (size_t i = 0; i < tmpl.size();) {
        if (tmpl[i] == '$') {
            size_t j = i + 1;
            int idx = 0;
            while (j < tmpl.size() && isdigit((unsigned char)tmpl[j])) {
                idx = idx * 10 + (tmpl[j] - '0');
                ++j;
            }
            if (j > i + 1 && idx < (int)inputExprs.size()) {
                out += inputExprs[idx];
                i = j;
                continue;
            }
        }
        if (tmpl.compare(i, 4, "@VAL") == 0) {
            out += "uN" + std::to_string(node.id);
            i += 4;
            continue;
        }
        if (tmpl.compare(i, 4, "@TEX") == 0) {
            auto it = ctx.samplerIndex.find(node.id);
            out += "uGraphTex" + std::to_string(it != ctx.samplerIndex.end() ? it->second : 0);
            i += 4;
            continue;
        }
        out += tmpl[i++];
    }
    return out;
}

// Depth-first topological sort from the output node. Returns false on cycle.
bool topoSort(const MaterialGraph& g, std::vector<const GraphNode*>& order, std::string& err) {
    enum class Mark { None, Temp, Done };
    std::unordered_map<int, Mark> marks;
    bool ok = true;

    std::function<void(const GraphNode*)> visit = [&](const GraphNode* n) {
        if (!ok || !n) return;
        Mark& m = marks[n->id];
        if (m == Mark::Done) return;
        if (m == Mark::Temp) {
            err = "Cycle detected in material graph";
            ok = false;
            return;
        }
        m = Mark::Temp;
        for (const Pin& in : n->inputs) {
            const Link* l = g.linkToInput(in.id);
            if (!l) continue;
            const GraphNode* src = g.findPin(l->fromPin, nullptr, nullptr);
            visit(src);
        }
        marks[n->id] = Mark::Done;
        if (n->id != g.outputNodeId()) order.push_back(n);
    };

    visit(g.findNode(g.outputNodeId()));
    return ok;
}

}  // namespace

std::vector<int> MaterialCodeGen::textureNodeOrder(const MaterialGraph& graph) {
    std::vector<int> ids;
    for (const auto& n : graph.nodes) {
        const NodeTypeDef* def = MaterialGraph::typeDef(n.type);
        if (def && def->hasTexture) ids.push_back(n.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::string MaterialCodeGen::generate(const MaterialGraph& graph, std::string& outError) {
    outError.clear();
    const GraphNode* out = graph.findNode(graph.outputNodeId());
    if (!out) {
        outError = "Material graph has no output node";
        return {};
    }

    GenContext ctx;
    ctx.graph = &graph;
    {
        auto texNodes = textureNodeOrder(graph);
        for (size_t i = 0; i < texNodes.size(); ++i) ctx.samplerIndex[texNodes[i]] = (int)i;
    }

    std::vector<const GraphNode*> order;
    if (!topoSort(graph, order, outError)) return {};

    std::ostringstream src;
    src << "#version 450 core\n";
    src << "// Auto-generated from a material node graph. Do not edit by hand.\n";
    // Generated files live in shaders/generated/, includes resolve relative
    // to the including file, hence the "..".
    src << "#include \"../graph_common.glsl\"\n\n";

    // ---- Uniform declarations ----
    for (const GraphNode* n : order) {
        const NodeTypeDef* def = MaterialGraph::typeDef(n->type);
        if (def && def->hasValue) src << "uniform vec4 uN" << n->id << ";\n";
    }
    for (const auto& [nodeId, idx] : ctx.samplerIndex)
        src << "uniform sampler2D uGraphTex" << idx << ";\n";
    src << "\n";

    // ---- Custom node functions (UE-style Custom HLSL) ----
    for (const GraphNode* n : order) {
        const NodeTypeDef* def = MaterialGraph::typeDef(n->type);
        if (!def || !def->hasCode) continue;
        std::string body = n->customCode;
        // Expression shorthand: no `return` -> treat the code as a vec4 expression.
        if (body.find("return") == std::string::npos) body = "return (" + body + ");";
        src << "vec4 g_custom_" << n->id << "(vec4 a, vec4 b, vec4 c, vec4 d) {\n"
            << body << "\n}\n\n";
    }

    src << "void main() {\n";
    src << "    g_prepare();\n\n";

    // ---- Graph statements in topological order ----
    for (const GraphNode* n : order) {
        const NodeTypeDef* def = MaterialGraph::typeDef(n->type);
        if (!def) continue;
        std::vector<std::string> ins;
        ins.reserve(n->inputs.size());
        for (const Pin& p : n->inputs) ins.push_back(resolveInput(ctx, p));
        if (def->hasCode) {
            src << "    vec4 " << outputVar(n->id, 0) << " = g_custom_" << n->id << "(" << ins[0]
                << ", " << ins[1] << ", " << ins[2] << ", " << ins[3] << ");\n";
            continue;
        }
        for (size_t k = 0; k < n->outputs.size() && k < def->exprs.size(); ++k) {
            src << "    vec4 " << outputVar(n->id, (int)k) << " = "
                << substitute(ctx, *n, def->exprs[k], ins) << ";\n";
        }
    }
    src << "\n";

    // ---- Output pins -> surface locals ----
    // Pin order must match the "Output" NodeTypeDef in MaterialGraph.cpp.
    static const char* surfNames[] = {
        "sBaseColor", "sMetallic", "sRoughness", "sNormalTS", "sEmissive", "sAO", "sAlpha",
        "sMask", "sShadowColor", "sShadowThreshold", "sShadowSoftness", "sRimColor", "sRimWidth",
        "sRimIntensity", "sSpecColor", "sSpecSize", "sSpecIntensity"};
    const size_t numPins = std::min(out->inputs.size(), (size_t)(sizeof(surfNames) / sizeof(*surfNames)));
    for (size_t i = 0; i < numPins; ++i)
        src << "    vec4 " << surfNames[i] << " = " << resolveInput(ctx, out->inputs[i]) << ";\n";
    src << "\n";

    // Opacity mask discard only when the pin is actually connected.
    if (graph.linkToInput(out->inputs[7].id))
        src << "    if (sMask.x < 0.5) discard;\n\n";

    src << R"GLSL(    // Tangent-space normal -> world
    vec3 nTS = clamp(sNormalTS.xyz, vec3(-1.0), vec3(1.0));
    vec3 T = normalize(fs.tangent.xyz - gNgeom * dot(gNgeom, fs.tangent.xyz));
    vec3 B = cross(gNgeom, T) * fs.tangent.w;
    vec3 N = normalize(mat3(T, B, gNgeom) * nTS);

    vec3 color = (uShadingModel == 1)
        ? evaluateToon(sBaseColor.xyz, clamp(sAO.x, 0.0, 1.0), N, gV,
                       sShadowColor.xyz, sShadowThreshold.x, sShadowSoftness.x,
                       sRimColor.xyz, sRimWidth.x, sRimIntensity.x,
                       sSpecColor.xyz, sSpecSize.x, sSpecIntensity.x)
        : evaluatePBR(sBaseColor.xyz, clamp(sMetallic.x, 0.0, 1.0),
                      clamp(sRoughness.x, 0.02, 1.0), clamp(sAO.x, 0.0, 1.0), N, gV);
    color += sEmissive.xyz;
    FragColor = vec4(color, clamp(sAlpha.x, 0.0, 1.0));
}
)GLSL";

    return src.str();
}
