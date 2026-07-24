#include "material/MaterialGraph.h"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "core/Log.h"
#include "render/Texture.h"

using json = nlohmann::json;

// ============================================================================
// Node type registry.
// All values flow as vec4 (UE-style implicit promotion): scalar results are
// splatted, vec2/vec3 results are padded. Exprs must therefore always yield
// a vec4. `gUV`, `gN` (geometric world normal), `gV` (view dir) and helpers
// g_* are provided by graph_common.glsl / the generated main().
// ============================================================================

const std::vector<NodeTypeDef>& MaterialGraph::nodeTypes() {
    using PT = PinType;
    static const std::vector<NodeTypeDef> defs = [] {
        std::vector<NodeTypeDef> d;

        auto add = [&d](NodeTypeDef def) { d.push_back(std::move(def)); };

        // ---- Output (fixed pins consumed by the lighting template) ----
        {
            NodeTypeDef o;
            o.type = "Output";
            o.category = "";
            o.inputs = {
                {"Base Color", PT::Vec3, {0.8f, 0.8f, 0.8f, 1.0f}},
                {"Metallic", PT::Float, {0, 0, 0, 0}},
                {"Roughness", PT::Float, {0.5f, 0, 0, 0}},
                {"Normal (TS)", PT::Vec3, {0, 0, 1, 0}},
                {"Emissive", PT::Vec3, {0, 0, 0, 0}},
                {"AO", PT::Float, {1, 0, 0, 0}},
                {"Alpha", PT::Float, {1, 0, 0, 0}},
                {"Opacity Mask", PT::Float, {1, 0, 0, 0}},
                {"Shadow Color", PT::Vec3, {0.62f, 0.54f, 0.72f, 1.0f}},
                {"Shadow Threshold", PT::Float, {0.5f, 0, 0, 0}},
                {"Shadow Softness", PT::Float, {0.05f, 0, 0, 0}},
                {"Rim Color", PT::Vec3, {1, 1, 1, 1}},
                {"Rim Width", PT::Float, {0.6f, 0, 0, 0}},
                {"Rim Intensity", PT::Float, {0.5f, 0, 0, 0}},
                {"Spec Color", PT::Vec3, {1, 1, 1, 1}},
                {"Spec Size", PT::Float, {0.1f, 0, 0, 0}},
                {"Spec Intensity", PT::Float, {0.6f, 0, 0, 0}},
            };
            add(o);
        }

        // ---- Constants ----
        auto constant = [&](const char* type, PinType pt, glm::vec4 def, const char* expr,
                            int comps, bool color) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Constant";
            n.outputs = {{"Value", pt, def}};
            n.exprs = {expr};
            n.hasValue = true;
            n.isColor = color;
            n.valueComponents = comps;
            add(n);
        };
        constant("Float", PT::Float, {0, 0, 0, 0}, "vec4(@VAL.x)", 1, false);
        constant("Vec2", PT::Vec2, {0, 0, 0, 0}, "vec4(@VAL.xy, 0.0, 0.0)", 2, false);
        constant("Vec3", PT::Vec3, {0, 0, 0, 0}, "vec4(@VAL.xyz, 1.0)", 3, false);
        constant("Vec4", PT::Vec4, {0, 0, 0, 0}, "@VAL", 4, false);
        constant("Color", PT::Vec3, {1, 1, 1, 1}, "vec4(@VAL.xyz, 1.0)", 3, true);

        // ---- Math (componentwise on vec4) ----
        auto math1 = [&](const char* type, const char* expr) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Math";
            n.inputs = {{"A", PT::Vec4, {0, 0, 0, 0}}};
            n.outputs = {{"Out", PT::Vec4}};
            n.exprs = {expr};
            add(n);
        };
        auto math2 = [&](const char* type, const char* expr, glm::vec4 defB = {0, 0, 0, 0}) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Math";
            n.inputs = {{"A", PT::Vec4, {0, 0, 0, 0}}, {"B", PT::Vec4, defB}};
            n.outputs = {{"Out", PT::Vec4}};
            n.exprs = {expr};
            add(n);
        };
        auto math3 = [&](const char* type, const char* n0, const char* n1, const char* n2,
                         glm::vec4 d0, glm::vec4 d1, glm::vec4 d2, const char* expr) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Math";
            n.inputs = {{n0, PT::Vec4, d0}, {n1, PT::Vec4, d1}, {n2, PT::Vec4, d2}};
            n.outputs = {{"Out", PT::Vec4}};
            n.exprs = {expr};
            add(n);
        };
        math2("Add", "($0 + $1)");
        math2("Subtract", "($0 - $1)");
        math2("Multiply", "($0 * $1)", {1, 1, 1, 1});
        math2("Divide", "($0 / max(abs($1), vec4(1e-5)) * sign($1 + vec4(equal($1, vec4(0.0)))))",
              {1, 1, 1, 1});
        math2("Power", "pow(max($0, vec4(0.0)), $1)", {2, 2, 2, 2});
        math2("Min", "min($0, $1)");
        math2("Max", "max($0, $1)");
        math2("Step", "step($0, $1)");
        math1("Sqrt", "sqrt(max($0, vec4(0.0)))");
        math1("Abs", "abs($0)");
        math1("Negate", "(-$0)");
        math1("OneMinus", "(vec4(1.0) - $0)");
        math1("Saturate", "clamp($0, vec4(0.0), vec4(1.0))");
        math1("Fract", "fract($0)");
        math1("Floor", "floor($0)");
        math1("Ceil", "ceil($0)");
        math1("Sin", "sin($0)");
        math1("Cos", "cos($0)");
        math3("Clamp", "X", "Min", "Max", {0, 0, 0, 0}, {0, 0, 0, 0}, {1, 1, 1, 1},
              "clamp($0, $1, $2)");
        math3("Lerp", "A", "B", "T", {0, 0, 0, 0}, {1, 1, 1, 1}, {0.5f, 0.5f, 0.5f, 0.5f},
              "mix($0, $1, $2)");
        math3("Smoothstep", "Edge0", "Edge1", "X", {0, 0, 0, 0}, {1, 1, 1, 1}, {0.5f, 0, 0, 0},
              "smoothstep($0, $1, $2)");

        // ---- Vector ----
        auto vecN = [&](const char* type, std::vector<PinDef> ins, std::vector<PinDef> outs,
                        std::vector<const char*> exprs) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Vector";
            n.inputs = std::move(ins);
            n.outputs = std::move(outs);
            n.exprs = std::move(exprs);
            add(n);
        };
        vecN("Dot", {{"A", PT::Vec3, {0, 0, 1, 0}}, {"B", PT::Vec3, {0, 0, 1, 0}}},
             {{"Out", PT::Float}}, {"vec4(dot($0.xyz, $1.xyz))"});
        vecN("Cross", {{"A", PT::Vec3}, {"B", PT::Vec3}}, {{"Out", PT::Vec3}},
             {"vec4(cross($0.xyz, $1.xyz), 0.0)"});
        vecN("Normalize", {{"A", PT::Vec3, {0, 0, 1, 0}}}, {{"Out", PT::Vec3}},
             {"vec4(normalize($0.xyz + vec3(1e-8)), $0.w)"});
        vecN("Length", {{"A", PT::Vec3}}, {{"Out", PT::Float}}, {"vec4(length($0.xyz))"});
        vecN("Split", {{"In", PT::Vec4}},
             {{"R", PT::Float}, {"G", PT::Float}, {"B", PT::Float}, {"A", PT::Float}},
             {"vec4($0.x)", "vec4($0.y)", "vec4($0.z)", "vec4($0.w)"});
        vecN("Combine",
             {{"R", PT::Float}, {"G", PT::Float}, {"B", PT::Float}, {"A", PT::Float, {1, 0, 0, 0}}},
             {{"Out", PT::Vec4}}, {"vec4($0.x, $1.x, $2.x, $3.x)"});

        // ---- Texture ----
        {
            NodeTypeDef n;
            n.type = "TextureSample";
            n.category = "Texture";
            n.inputs = {{"UV", PT::Vec2, {0, 0, 0, 0}, "vec4(gUV, 0.0, 0.0)"}};
            n.outputs = {{"Color", PT::Vec4}};
            n.exprs = {"texture(@TEX, ($0).xy)"};
            n.hasTexture = true;
            add(n);
        }
        {
            NodeTypeDef n;
            n.type = "NormalUnpack";
            n.category = "Texture";
            n.inputs = {{"Color", PT::Vec4, {0.5f, 0.5f, 1.0f, 1.0f}},
                        {"Strength", PT::Float, {1, 0, 0, 0}}};
            n.outputs = {{"Normal (TS)", PT::Vec3}};
            n.exprs = {"vec4(normalize((($0).xyz * 2.0 - 1.0) * vec3($1.x, $1.x, 1.0)), 0.0)"};
            add(n);
        }

        // ---- UV ----
        auto uvN = [&](const char* type, std::vector<PinDef> ins, const char* expr) {
            NodeTypeDef n;
            n.type = type;
            n.category = "UV";
            n.inputs = std::move(ins);
            n.outputs = {{"UV", PT::Vec2}};
            n.exprs = {expr};
            add(n);
        };
        uvN("UV", {}, "vec4(gUV, 0.0, 0.0)");
        uvN("TilingOffset",
            {{"UV", PT::Vec2, {0, 0, 0, 0}, "vec4(gUV, 0.0, 0.0)"},
             {"Tiling", PT::Vec2, {1, 1, 0, 0}},
             {"Offset", PT::Vec2, {0, 0, 0, 0}}},
            "vec4(($0).xy * ($1).xy + ($2).xy, 0.0, 0.0)");
        uvN("RotateUV",
            {{"UV", PT::Vec2, {0, 0, 0, 0}, "vec4(gUV, 0.0, 0.0)"},
             {"Angle (rad)", PT::Float, {0, 0, 0, 0}},
             {"Center", PT::Vec2, {0.5f, 0.5f, 0, 0}}},
            "vec4(g_rotateUV(($0).xy, $1.x, ($2).xy), 0.0, 0.0)");
        uvN("Panner",
            {{"UV", PT::Vec2, {0, 0, 0, 0}, "vec4(gUV, 0.0, 0.0)"},
             {"Speed", PT::Vec2, {0.1f, 0.1f, 0, 0}}},
            "vec4(($0).xy + ($1).xy * uTime, 0.0, 0.0)");

        // ---- Utility ----
        auto util = [&](const char* type, std::vector<PinDef> ins, PinType outType,
                        const char* expr) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Utility";
            n.inputs = std::move(ins);
            n.outputs = {{"Out", outType}};
            n.exprs = {expr};
            add(n);
        };
        util("Fresnel", {{"Exponent", PT::Float, {5, 0, 0, 0}}}, PT::Float,
             "vec4(pow(1.0 - clamp(dot(gN, gV), 0.0, 1.0), max($0.x, 1e-3)))");
        util("ViewDirection", {}, PT::Vec3, "vec4(gV, 0.0)");
        util("WorldNormal", {}, PT::Vec3, "vec4(gN, 0.0)");
        util("WorldPosition", {}, PT::Vec3, "vec4(fs.worldPos, 1.0)");
        util("ScreenPosition", {}, PT::Vec2,
             "vec4(gl_FragCoord.xy / max(uViewportSize, vec2(1.0)), 0.0, 0.0)");
        util("Time", {}, PT::Float, "vec4(uTime)");
        util("CameraDistance", {}, PT::Float, "vec4(length(uCameraPos - fs.worldPos))");
        util("SunNdotL", {}, PT::Float, "vec4(dot(gN, -normalize(uSunDirection)))");

        // UE-style Custom node: user writes the body of
        //   vec4 f(vec4 a, vec4 b, vec4 c, vec4 d)
        // HLSL-ish aliases (float2/3/4, lerp, saturate, frac, mul) are
        // #defined in graph_common.glsl; globals gN/gV/gUV/uTime are usable.
        {
            NodeTypeDef n;
            n.type = "Custom";
            n.category = "Utility";
            n.inputs = {{"a", PT::Vec4, {0, 0, 0, 0}},
                        {"b", PT::Vec4, {0, 0, 0, 0}},
                        {"c", PT::Vec4, {0, 0, 0, 0}},
                        {"d", PT::Vec4, {0, 0, 0, 0}}};
            n.outputs = {{"Out", PT::Vec4}};
            n.hasCode = true;
            n.defaultCode = "// vec4 f(a, b, c, d) — HLSL aliases: float3, lerp,\n"
                            "// saturate, frac, mul. Globals: gN, gV, gUV, uTime.\n"
                            "return lerp(a, b, saturate(c.x));";
            add(n);
        }

        // ---- Noise ----
        auto noise = [&](const char* type, const char* fn) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Noise";
            n.inputs = {{"UV", PT::Vec2, {0, 0, 0, 0}, "vec4(gUV, 0.0, 0.0)"},
                        {"Scale", PT::Float, {8, 0, 0, 0}}};
            n.outputs = {{"Out", PT::Float}};
            static std::string buf[8];
            static int bufIdx = 0;
            std::string& s = buf[bufIdx++ % 8];
            s = std::string("vec4(") + fn + "(($0).xy * $1.x))";
            n.exprs = {s.c_str()};
            add(n);
        };
        noise("PerlinNoise", "g_perlin");
        noise("ValueNoise", "g_valueNoise");
        noise("VoronoiNoise", "g_voronoi");

        // ---- Toon / NPR (Phase 4) ----
        auto toon = [&](const char* type, std::vector<PinDef> ins, PinType outType,
                        const char* expr, bool hasTex = false) {
            NodeTypeDef n;
            n.type = type;
            n.category = "Toon";
            n.inputs = std::move(ins);
            n.outputs = {{"Out", outType}};
            n.exprs = {expr};
            n.hasTexture = hasTex;
            add(n);
        };
        toon("RampShade",
             {{"Lambert", PT::Float, {0, 0, 0, 0}, "vec4(dot(gN, -normalize(uSunDirection)) * 0.5 + 0.5)"},
              {"Threshold", PT::Float, {0.5f, 0, 0, 0}},
              {"Softness", PT::Float, {0.05f, 0, 0, 0}}},
             PT::Float, "vec4(smoothstep($1.x - $2.x, $1.x + $2.x, $0.x))");
        toon("SDFFaceShadow",
             {{"SDF", PT::Float, {1, 0, 0, 0}},
              {"Softness", PT::Float, {0.02f, 0, 0, 0}}},
             PT::Float, "vec4(g_sdfFaceShadow($0.x, $1.x))");
        toon("AnisoHighlight",
             {{"Shift", PT::Float, {0, 0, 0, 0}},
              {"Exponent", PT::Float, {64, 0, 0, 0}}},
             PT::Float, "vec4(g_kajiyaKay($0.x, $1.x))");
        toon("StylizedSpecular",
             {{"Size", PT::Float, {0.1f, 0, 0, 0}},
              {"Hardness", PT::Float, {0.02f, 0, 0, 0}}},
             PT::Float, "vec4(g_stylizedSpec($0.x, $1.x))");
        toon("RimLight",
             {{"Width", PT::Float, {0.6f, 0, 0, 0}},
              {"LitSideBias", PT::Float, {1, 0, 0, 0}}},
             PT::Float, "vec4(g_rim($0.x, $1.x))");
        toon("Matcap", {}, PT::Vec3, "vec4(texture(@TEX, g_matcapUV()).rgb, 1.0)", true);
        toon("SubsurfaceApprox",
             {{"SSS Color", PT::Vec3, {0.9f, 0.3f, 0.2f, 1.0f}},
              {"Wrap", PT::Float, {0.5f, 0, 0, 0}}},
             PT::Vec3, "vec4(g_sss(($0).xyz, $1.x), 1.0)");

        return d;
    }();
    return defs;
}

const NodeTypeDef* MaterialGraph::typeDef(const std::string& type) {
    for (const auto& d : nodeTypes())
        if (type == d.type) return &d;
    return nullptr;
}

// ============================================================================
// Graph editing
// ============================================================================

MaterialGraph::MaterialGraph() {
    GraphNode& out = addNode("Output", {520.0f, 120.0f});
    outputNodeId_ = out.id;
}

GraphNode& MaterialGraph::addNode(const std::string& type, glm::vec2 pos) {
    const NodeTypeDef* def = typeDef(type);
    GraphNode node;
    node.id = nextId++;
    node.type = type;
    node.position = pos;
    if (def) {
        for (const PinDef& p : def->inputs) {
            Pin pin;
            pin.id = nextId++;
            pin.name = p.name;
            pin.type = p.type;
            pin.defaultValue = p.def;
            if (p.defExpr) pin.defaultExpr = p.defExpr;
            node.inputs.push_back(std::move(pin));
        }
        for (const PinDef& p : def->outputs) {
            Pin pin;
            pin.id = nextId++;
            pin.name = p.name;
            pin.type = p.type;
            node.outputs.push_back(std::move(pin));
        }
        if (def->isColor) node.value = def->outputs.empty() ? glm::vec4(1.0f) : def->outputs[0].def;
        if (def->hasCode && def->defaultCode) node.customCode = def->defaultCode;
    }
    nodes.push_back(std::move(node));
    dirty = true;
    return nodes.back();
}

void MaterialGraph::removeNode(int nodeId) {
    if (nodeId == outputNodeId_) return;  // output node is fixed
    const GraphNode* n = findNode(nodeId);
    if (!n) return;
    // Drop all links touching this node's pins.
    auto touches = [n](const Link& l) {
        for (const Pin& p : n->inputs)
            if (l.toPin == p.id || l.fromPin == p.id) return true;
        for (const Pin& p : n->outputs)
            if (l.toPin == p.id || l.fromPin == p.id) return true;
        return false;
    };
    links.erase(std::remove_if(links.begin(), links.end(), touches), links.end());
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(),
                               [nodeId](const GraphNode& g) { return g.id == nodeId; }),
                nodes.end());
    dirty = true;
}

bool MaterialGraph::addLink(int pinA, int pinB) {
    bool aIsInput = false, bIsInput = false;
    int ai = -1, bi = -1;
    const GraphNode* na = findPin(pinA, &aIsInput, &ai);
    const GraphNode* nb = findPin(pinB, &bIsInput, &bi);
    if (!na || !nb || na == nb) return false;
    if (aIsInput == bIsInput) return false;  // need one output + one input

    int fromPin = aIsInput ? pinB : pinA;
    int toPin = aIsInput ? pinA : pinB;
    const GraphNode* fromNode = aIsInput ? nb : na;
    const GraphNode* toNode = aIsInput ? na : nb;

    // Reject cycles: the target node must not (transitively) feed the source.
    if (reachesNode(fromNode->id, toNode->id)) {
        Log::warn("Material graph: link rejected (would create a cycle)");
        return false;
    }

    // One link per input: replace existing.
    links.erase(std::remove_if(links.begin(), links.end(),
                               [toPin](const Link& l) { return l.toPin == toPin; }),
                links.end());
    Link l;
    l.id = nextId++;
    l.fromPin = fromPin;
    l.toPin = toPin;
    links.push_back(l);
    dirty = true;
    return true;
}

void MaterialGraph::removeLink(int linkId) {
    size_t before = links.size();
    links.erase(std::remove_if(links.begin(), links.end(),
                               [linkId](const Link& l) { return l.id == linkId; }),
                links.end());
    if (links.size() != before) dirty = true;
}

GraphNode* MaterialGraph::findNode(int id) {
    for (auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const GraphNode* MaterialGraph::findNode(int id) const {
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const GraphNode* MaterialGraph::findPin(int pinId, bool* outIsInput, int* outPinIndex) const {
    for (const auto& n : nodes) {
        for (size_t i = 0; i < n.inputs.size(); ++i)
            if (n.inputs[i].id == pinId) {
                if (outIsInput) *outIsInput = true;
                if (outPinIndex) *outPinIndex = (int)i;
                return &n;
            }
        for (size_t i = 0; i < n.outputs.size(); ++i)
            if (n.outputs[i].id == pinId) {
                if (outIsInput) *outIsInput = false;
                if (outPinIndex) *outPinIndex = (int)i;
                return &n;
            }
    }
    return nullptr;
}

const Link* MaterialGraph::linkToInput(int inputPinId) const {
    for (const auto& l : links)
        if (l.toPin == inputPinId) return &l;
    return nullptr;
}

// Does data flow from `targetNode` reach `startNode`'s inputs? Used inverted:
// reachesNode(from, to) checks whether `to`'s outputs eventually feed `from`.
bool MaterialGraph::reachesNode(int startNode, int targetNode) const {
    if (startNode == targetNode) return true;
    const GraphNode* start = findNode(startNode);
    if (!start) return false;
    for (const Pin& in : start->inputs) {
        const Link* l = linkToInput(in.id);
        if (!l) continue;
        const GraphNode* src = findPin(l->fromPin, nullptr, nullptr);
        if (src && reachesNode(src->id, targetNode)) return true;
    }
    return false;
}

// ============================================================================
// JSON serialization
// ============================================================================

void MaterialGraph::toJson(json& j) const {
    j["nextId"] = nextId;
    j["output"] = outputNodeId_;
    json jn = json::array();
    for (const auto& n : nodes) {
        json e;
        e["id"] = n.id;
        e["type"] = n.type;
        e["pos"] = {n.position.x, n.position.y};
        e["value"] = {n.value.x, n.value.y, n.value.z, n.value.w};
        if (!n.texturePath.empty()) e["texture"] = n.texturePath;
        if (!n.customCode.empty()) e["code"] = n.customCode;
        e["srgb"] = n.srgb;
        json in = json::array(), out = json::array();
        for (const Pin& p : n.inputs) in.push_back(p.id);
        for (const Pin& p : n.outputs) out.push_back(p.id);
        e["in"] = in;
        e["out"] = out;
        jn.push_back(e);
    }
    j["nodes"] = jn;
    json jl = json::array();
    for (const auto& l : links) jl.push_back({{"id", l.id}, {"from", l.fromPin}, {"to", l.toPin}});
    j["links"] = jl;
}

std::shared_ptr<MaterialGraph> MaterialGraph::fromJson(const json& j) {
    auto g = std::make_shared<MaterialGraph>();
    g->nodes.clear();
    g->links.clear();
    g->nextId = j.value("nextId", 1);
    g->outputNodeId_ = j.value("output", 0);

    if (j.contains("nodes") && j["nodes"].is_array()) {
        for (const auto& e : j["nodes"]) {
            std::string type = e.value("type", "");
            const NodeTypeDef* def = typeDef(type);
            if (!def) {
                Log::warn("Material graph: unknown node type '%s' skipped", type.c_str());
                continue;
            }
            GraphNode n;
            n.id = e.value("id", 0);
            n.type = type;
            if (e.contains("pos") && e["pos"].is_array() && e["pos"].size() >= 2)
                n.position = {e["pos"][0].get<float>(), e["pos"][1].get<float>()};
            if (e.contains("value") && e["value"].is_array() && e["value"].size() >= 4)
                n.value = {e["value"][0].get<float>(), e["value"][1].get<float>(),
                           e["value"][2].get<float>(), e["value"][3].get<float>()};
            n.srgb = e.value("srgb", true);
            n.customCode = e.value("code", std::string());
            if (n.customCode.empty() && def->hasCode && def->defaultCode)
                n.customCode = def->defaultCode;
            n.texturePath = e.value("texture", std::string());
            if (!n.texturePath.empty()) n.texture = Texture::load2D(n.texturePath, n.srgb);

            // Rebuild pins from the registry; restore persisted ids where the
            // pin count still matches (otherwise links to this node are lost).
            for (size_t i = 0; i < def->inputs.size(); ++i) {
                Pin p;
                p.id = (e.contains("in") && i < e["in"].size()) ? e["in"][i].get<int>()
                                                                : g->nextId++;
                p.name = def->inputs[i].name;
                p.type = def->inputs[i].type;
                p.defaultValue = def->inputs[i].def;
                if (def->inputs[i].defExpr) p.defaultExpr = def->inputs[i].defExpr;
                n.inputs.push_back(std::move(p));
            }
            for (size_t i = 0; i < def->outputs.size(); ++i) {
                Pin p;
                p.id = (e.contains("out") && i < e["out"].size()) ? e["out"][i].get<int>()
                                                                  : g->nextId++;
                p.name = def->outputs[i].name;
                p.type = def->outputs[i].type;
                n.outputs.push_back(std::move(p));
            }
            g->nodes.push_back(std::move(n));
        }
    }
    if (j.contains("links") && j["links"].is_array()) {
        for (const auto& e : j["links"]) {
            Link l;
            l.id = e.value("id", 0);
            l.fromPin = e.value("from", 0);
            l.toPin = e.value("to", 0);
            // Validate both endpoints still exist.
            if (g->findPin(l.fromPin, nullptr, nullptr) && g->findPin(l.toPin, nullptr, nullptr))
                g->links.push_back(l);
        }
    }
    // Guarantee an output node exists.
    if (!g->findNode(g->outputNodeId_)) {
        GraphNode& out = g->addNode("Output", {520.0f, 120.0f});
        g->outputNodeId_ = out.id;
    }
    g->dirty = true;
    return g;
}
