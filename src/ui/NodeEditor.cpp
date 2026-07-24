#include "ui/NodeEditor.h"

#include <imgui.h>
#include <imnodes.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "material/MaterialGraph.h"
#include "render/Shader.h"
#include "render/Texture.h"
#include "scene/Material.h"
#include "scene/Node.h"
#include "scene/Scene.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace {

std::string openImageDialog() {
#ifdef _WIN32
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp\0All Files\0*.*\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (GetOpenFileNameW(&ofn)) {
        int n = WideCharToMultiByte(CP_UTF8, 0, file, -1, nullptr, 0, nullptr, nullptr);
        std::string out(n > 0 ? n - 1 : 0, '\0');
        WideCharToMultiByte(CP_UTF8, 0, file, -1, out.data(), n, nullptr, nullptr);
        return out;
    }
#endif
    return {};
}

unsigned pinColor(PinType t) {
    switch (t) {
        case PinType::Float: return IM_COL32(160, 210, 160, 255);
        case PinType::Vec2:  return IM_COL32(130, 190, 230, 255);
        case PinType::Vec3:  return IM_COL32(230, 200, 120, 255);
        case PinType::Vec4:  return IM_COL32(220, 150, 220, 255);
        case PinType::Texture2D: return IM_COL32(230, 130, 130, 255);
    }
    return IM_COL32(200, 200, 200, 255);
}

}  // namespace

void NodeEditor::draw(Scene& scene) {
    ImGui::SetNextWindowSize(ImVec2(960, 540), ImGuiCond_FirstUseEver);
    ImGui::Begin("Material Graph");

    auto sel = scene.selected;
    Material* mat = sel && sel->material ? sel->material.get() : nullptr;
    if (!mat) {
        ImGui::TextDisabled("Select a node with a material to edit its graph.");
        ImGui::End();
        return;
    }

    if (!mat->graph) {
        ImGui::TextWrapped("Material '%s' uses the standard parameter shader.", mat->name.c_str());
        if (ImGui::Button("Create Material Graph")) {
            mat->graph = std::make_shared<MaterialGraph>();
        }
        ImGui::End();
        return;
    }

    MaterialGraph& graph = *mat->graph;

    // ---- Toolbar ----
    if (ImGui::Button("Recompile")) graph.dirty = true;
    ImGui::SameLine();
    if (ImGui::Button("Delete Graph")) {
        mat->graph = nullptr;
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (!graph.lastError.empty())
        ImGui::TextColored({1.0f, 0.35f, 0.35f, 1.0f}, "Error: %s", graph.lastError.c_str());
    else if (graph.compiled && graph.compiled->valid())
        ImGui::TextColored({0.4f, 0.9f, 0.4f, 1.0f}, "Compiled OK");
    else
        ImGui::TextDisabled("Not compiled yet");

    // ---- Node canvas ----
    ImNodes::BeginNodeEditor();
    drawGraphNodes(graph);
    for (const Link& l : graph.links) ImNodes::Link(l.id, l.fromPin, l.toPin);
    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    ImNodes::EndNodeEditor();

    // Persist node positions moved by the user.
    for (GraphNode& n : graph.nodes) {
        ImVec2 p = ImNodes::GetNodeGridSpacePos(n.id);
        n.position = {p.x, p.y};
    }

    handleInteractions(graph);
    addNodePopup(graph);

    ImGui::End();
}

void NodeEditor::drawGraphNodes(MaterialGraph& graph) {
    for (GraphNode& n : graph.nodes) {
        const NodeTypeDef* def = MaterialGraph::typeDef(n.type);
        if (!def) continue;

        if (n.posDirty) {
            ImNodes::SetNodeGridSpacePos(n.id, ImVec2(n.position.x, n.position.y));
            n.posDirty = false;
        }

        bool isOutput = n.id == graph.outputNodeId();
        if (isOutput)
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(140, 60, 60, 255));

        ImNodes::BeginNode(n.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(isOutput ? "Material Output" : n.type.c_str());
        ImNodes::EndNodeTitleBar();

        // ---- Inline value editors ----
        if (def->hasValue) {
            ImGui::PushItemWidth(140.0f);
            if (def->isColor) {
                ImGui::ColorEdit3("##v", &n.value.x,
                                  ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            } else {
                switch (def->valueComponents) {
                    case 1: ImGui::DragFloat("##v", &n.value.x, 0.01f); break;
                    case 2: ImGui::DragFloat2("##v", &n.value.x, 0.01f); break;
                    case 3: ImGui::DragFloat3("##v", &n.value.x, 0.01f); break;
                    default: ImGui::DragFloat4("##v", &n.value.x, 0.01f); break;
                }
            }
            ImGui::PopItemWidth();
        }
        if (def->hasTexture) {
            if (n.texture)
                ImGui::Image((ImTextureID)(intptr_t)n.texture->id(), ImVec2(48, 48));
            else
                ImGui::TextDisabled("(no texture)");
            if (ImGui::SmallButton("Load##tex")) {
                std::string p = openImageDialog();
                if (!p.empty()) {
                    auto t = Texture::load2D(p, n.srgb);
                    if (t) {
                        n.texture = t;
                        n.texturePath = p;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Checkbox("sRGB", &n.srgb) && !n.texturePath.empty())
                n.texture = Texture::load2D(n.texturePath, n.srgb);
        }
        if (def->hasCode) {
            ImGui::TextDisabled("vec4 f(vec4 a, b, c, d)  [GLSL / HLSL-ish]");
            ImGui::InputTextMultiline("##code", &n.customCode, ImVec2(300, 96),
                                      ImGuiInputTextFlags_AllowTabInput);
            if (ImGui::SmallButton("Apply##code")) graph.dirty = true;
            ImGui::SameLine();
            ImGui::TextDisabled("(recompiles the shader)");
        }

        // ---- Pins ----
        for (const Pin& p : n.inputs) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, pinColor(p.type));
            ImNodes::BeginInputAttribute(p.id, ImNodesPinShape_CircleFilled);
            ImGui::TextUnformatted(p.name.c_str());
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        for (const Pin& p : n.outputs) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, pinColor(p.type));
            ImNodes::BeginOutputAttribute(p.id, ImNodesPinShape_CircleFilled);
            float w = ImGui::CalcTextSize(p.name.c_str()).x;
            ImGui::Indent(std::max(120.0f - w, 0.0f));
            ImGui::TextUnformatted(p.name.c_str());
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }

        ImNodes::EndNode();
        if (isOutput) ImNodes::PopColorStyle();
    }
}

void NodeEditor::handleInteractions(MaterialGraph& graph) {
    int a = 0, b = 0;
    if (ImNodes::IsLinkCreated(&a, &b)) graph.addLink(a, b);

    int linkId = 0;
    if (ImNodes::IsLinkDestroyed(&linkId)) graph.removeLink(linkId);

    // Delete selected links/nodes with the Delete key.
    if (ImGui::IsKeyPressed(ImGuiKey_Delete) && ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows)) {
        int numLinks = ImNodes::NumSelectedLinks();
        if (numLinks > 0) {
            std::vector<int> ids(numLinks);
            ImNodes::GetSelectedLinks(ids.data());
            for (int id : ids) graph.removeLink(id);
        }
        int numNodes = ImNodes::NumSelectedNodes();
        if (numNodes > 0) {
            std::vector<int> ids(numNodes);
            ImNodes::GetSelectedNodes(ids.data());
            for (int id : ids) graph.removeNode(id);  // output node is protected
        }
        ImNodes::ClearLinkSelection();
        ImNodes::ClearNodeSelection();
    }
}

void NodeEditor::addNodePopup(MaterialGraph& graph) {
    static ImVec2 spawnPos;
    if (ImNodes::IsEditorHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
        spawnPos = ImGui::GetMousePos();
        ImGui::OpenPopup("add_graph_node");
    }
    if (ImGui::BeginPopup("add_graph_node")) {
        static const char* categories[] = {"Constant", "Math",  "Vector", "Texture",
                                           "UV",       "Utility", "Noise",  "Toon"};
        for (const char* cat : categories) {
            if (ImGui::BeginMenu(cat)) {
                for (const NodeTypeDef& def : MaterialGraph::nodeTypes()) {
                    if (std::strcmp(def.category, cat) != 0) continue;
                    if (ImGui::MenuItem(def.type)) {
                        GraphNode& n = graph.addNode(def.type, {0, 0});
                        ImNodes::SetNodeScreenSpacePos(n.id, spawnPos);
                        ImVec2 grid = ImNodes::GetNodeGridSpacePos(n.id);
                        n.position = {grid.x, grid.y};
                        n.posDirty = false;
                    }
                }
                ImGui::EndMenu();
            }
        }
        ImGui::EndPopup();
    }
}
