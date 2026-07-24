#include "ui/EditorUI.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>

#include "core/EventBus.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "core/Window.h"
#include "import/ModelLoader.h"
#include "render/ShaderLibrary.h"
#include "render/Texture.h"
#include "scene/Material.h"
#include "scene/Mesh.h"
#include "scene/Scene.h"

#ifdef _WIN32
#include <windows.h>
#include <commdlg.h>
#endif

static ImTextureID toImTex(GLuint tex) { return (ImTextureID)(intptr_t)tex; }

static std::string openFileDialog(const char* filter) {
#ifdef _WIN32
    char file[MAX_PATH] = {};
    OPENFILENAMEA ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameA(&ofn)) return file;
#endif
    return {};
}

void EditorUI::init(Window& window, Scene& scene, Renderer& renderer, ShaderLibrary& shaders,
                    EventBus& bus) {
    window_ = &window;
    scene_ = &scene;
    renderer_ = &renderer;
    shaders_ = &shaders;
    bus_ = &bus;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    setupStyle();

    ImGui_ImplGlfw_InitForOpenGL(window.handle(), true);
    ImGui_ImplOpenGL3_Init("#version 450");

    brdfPath_ = Paths::shader("brdf.glsl");
    brdfBuffer_.resize(64 * 1024, 0);
}

void EditorUI::setupStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& s = ImGui::GetStyle();
    s.WindowRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.GrabRounding = 3.0f;
    s.TabRounding = 3.0f;
    s.WindowPadding = {8, 8};
    s.FramePadding = {6, 3};
    s.ItemSpacing = {6, 5};

    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = {0.11f, 0.11f, 0.12f, 1.0f};
    c[ImGuiCol_TitleBgActive] = {0.16f, 0.16f, 0.18f, 1.0f};
    c[ImGuiCol_Header] = {0.24f, 0.26f, 0.32f, 0.8f};
    c[ImGuiCol_HeaderHovered] = {0.30f, 0.33f, 0.42f, 0.9f};
    c[ImGuiCol_FrameBg] = {0.17f, 0.17f, 0.19f, 1.0f};
    c[ImGuiCol_FrameBgHovered] = {0.22f, 0.22f, 0.26f, 1.0f};
    c[ImGuiCol_Button] = {0.24f, 0.26f, 0.32f, 0.8f};
    c[ImGuiCol_ButtonHovered] = {0.32f, 0.36f, 0.46f, 1.0f};
    c[ImGuiCol_Tab] = {0.13f, 0.13f, 0.15f, 1.0f};
    c[ImGuiCol_TabSelected] = {0.24f, 0.26f, 0.34f, 1.0f};
    c[ImGuiCol_DockingPreview] = {0.35f, 0.45f, 0.75f, 0.6f};
}

void EditorUI::shutdown() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void EditorUI::beginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void EditorUI::endFrame() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void EditorUI::buildDefaultLayout(unsigned dockspaceId) {
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.32f, nullptr, &center);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.5f, nullptr, &right);

    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Lights", rightBottom);
    ImGui::DockBuilderDockWindow("Environment", rightBottom);
    ImGui::DockBuilderDockWindow("Post FX", rightBottom);
    ImGui::DockBuilderDockWindow("Frame Debugger", bottom);
    ImGui::DockBuilderDockWindow("BRDF Editor", bottom);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderFinish(dockspaceId);
}

void EditorUI::draw(float dt) {
    dt_ = dt;

    // Fullscreen host window with a dockspace
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);
    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                                 ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("DockHost", nullptr, hostFlags);
    ImGui::PopStyleVar(2);

    drawMenuBar();

    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    if (!layoutInitialized_) {
        layoutInitialized_ = true;
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) buildDefaultLayout(dockspaceId);
    }
    ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
    ImGui::End();

    drawViewport();
    drawHierarchy();
    drawInspector();
    drawLights();
    drawEnvironment();
    drawPostProcessing();
    drawBRDFEditor();
    drawFrameDebugger();
    drawConsole();
}

void EditorUI::drawMenuBar() {
    if (!ImGui::BeginMenuBar()) return;
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import Model...", "Ctrl+O")) importModelDialog();
        if (ImGui::MenuItem("Load HDR Environment...")) loadHdrDialog();
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window_->handle(), 1);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Scene")) {
        if (ImGui::MenuItem("Add Sphere")) {
            auto n = scene_->root->addChild("Sphere");
            n->mesh = Mesh::sphere(0.5f, 48, 32);
            n->material = std::make_shared<Material>();
            n->position = {0, 1, 0};
            scene_->selected = n;
        }
        if (ImGui::MenuItem("Add Cube")) {
            auto n = scene_->root->addChild("Cube");
            n->mesh = Mesh::cube(1.0f);
            n->material = std::make_shared<Material>();
            n->position = {0, 1, 0};
            scene_->selected = n;
        }
        if (ImGui::MenuItem("Add Plane")) {
            auto n = scene_->root->addChild("Plane");
            n->mesh = Mesh::plane(10.0f);
            n->material = std::make_shared<Material>();
            scene_->selected = n;
        }
        ImGui::EndMenu();
    }

    // FPS + GPU time on the right side of the menu bar
    char stats[128];
    snprintf(stats, sizeof(stats), "GPU %.2f ms | %.0f FPS", renderer_->frameDebugger().totalGpuMs(),
             dt_ > 0 ? 1.0f / dt_ : 0.0f);
    float w = ImGui::CalcTextSize(stats).x + 16;
    ImGui::SetCursorPosX(ImGui::GetWindowWidth() - w);
    ImGui::TextDisabled("%s", stats);
    ImGui::EndMenuBar();
}

void EditorUI::drawViewport() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    vpDesiredW_ = std::max((int)avail.x, 8);
    vpDesiredH_ = std::max((int)avail.y, 8);

    // Final texture is rendered bottom-up (GL), flip V for display.
    ImGui::Image(toImTex(renderer_->finalTexture()), avail, ImVec2(0, 1), ImVec2(1, 0));

    // Camera controls when hovering the viewport
    if (ImGui::IsItemHovered()) {
        ImGuiIO& io = ImGui::GetIO();
        Camera& cam = scene_->camera;
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !io.KeyShift)
            cam.orbit(io.MouseDelta.x, io.MouseDelta.y);
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
            (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && io.KeyShift))
            cam.pan(io.MouseDelta.x, io.MouseDelta.y);
        if (io.MouseWheel != 0.0f) cam.zoom(io.MouseWheel);
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void EditorUI::drawHierarchy() {
    ImGui::Begin("Hierarchy");
    if (ImGui::Button("+ Sphere")) {
        auto n = scene_->root->addChild("Sphere");
        n->mesh = Mesh::sphere(0.5f, 48, 32);
        n->material = std::make_shared<Material>();
        n->position = {0, 1, 0};
        scene_->selected = n;
    }
    ImGui::SameLine();
    if (ImGui::Button("+ Import...")) importModelDialog();
    ImGui::Separator();
    drawNodeTree(scene_->root);
    ImGui::End();
}

void EditorUI::drawNodeTree(const std::shared_ptr<Node>& node) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (node->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (scene_->selected == node) flags |= ImGuiTreeNodeFlags_Selected;
    if (node == scene_->root) flags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushID(node.get());
    if (!node->visible) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
    bool open = ImGui::TreeNodeEx(node->name.c_str(), flags);
    if (!node->visible) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) scene_->selected = node;

    if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem(node->visible ? "Hide" : "Show")) node->visible = !node->visible;
        if (node != scene_->root && ImGui::MenuItem("Delete")) {
            if (scene_->selected == node) scene_->selected = nullptr;
            if (node->parent) node->parent->removeChild(node.get());
            ImGui::EndPopup();
            ImGui::PopID();
            if (open) ImGui::TreePop();
            return;
        }
        ImGui::EndPopup();
    }

    if (open) {
        auto children = node->children;  // copy: children may be deleted while iterating
        for (auto& c : children) drawNodeTree(c);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void EditorUI::drawInspector() {
    ImGui::Begin("Inspector");
    auto node = scene_->selected;
    if (!node) {
        ImGui::TextDisabled("Select a node in the Hierarchy");
        ImGui::End();
        return;
    }

    char nameBuf[128];
    snprintf(nameBuf, sizeof(nameBuf), "%s", node->name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) node->name = nameBuf;
    ImGui::Checkbox("Visible", &node->visible);

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &node->position.x, 0.01f);
        ImGui::DragFloat3("Rotation", &node->rotationEuler.x, 0.5f);
        ImGui::DragFloat3("Scale", &node->scale.x, 0.01f, 0.0001f, 1000.0f);
    }

    if (node->material) drawMaterialEditor(*node);
    if (node->mesh) {
        ImGui::Separator();
        ImGui::TextDisabled("Mesh: %s  (%u verts, %u tris)", node->mesh->name().c_str(),
                            node->mesh->vertexCount(), node->mesh->indexCount() / 3);
    }
    ImGui::End();
}

static void textureSlot(const char* label, std::shared_ptr<Texture>& slot, bool srgb) {
    ImGui::PushID(label);
    if (slot)
        ImGui::Image(toImTex(slot->id()), ImVec2(28, 28));
    else {
        ImGui::Dummy(ImVec2(28, 28));
        ImGui::SameLine(10);
        ImGui::TextDisabled("-");
        ImGui::SameLine(30);
        ImGui::Dummy(ImVec2(0, 0));
    }
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s", label);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70);
    if (ImGui::SmallButton("Load")) {
        std::string p = openFileDialog("Images\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds\0All\0*.*\0");
        if (!p.empty()) {
            auto t = Texture::load2D(p, srgb);
            if (t) slot = t;
        }
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("X")) slot = nullptr;
    ImGui::PopID();
}

void EditorUI::drawMaterialEditor(Node& node) {
    Material& m = *node.material;
    if (!ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) return;

    ImGui::Text("%s", m.name.c_str());
    int model = (int)m.model;
    if (ImGui::Combo("Shading Model", &model, "PBR (Cook-Torrance)\0Toon (ZZZ/Endfield)\0"))
        m.model = (ShadingModel)model;

    ImGui::ColorEdit3("Base Color", &m.baseColor.x);
    if (m.model == ShadingModel::PBR) {
        ImGui::SliderFloat("Metallic", &m.metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &m.roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Specular F0", &m.specularF0, 0.0f, 0.16f);
        ImGui::SliderFloat("IBL Intensity", &m.iblIntensity, 0.0f, 3.0f);
    } else {
        ImGui::ColorEdit3("Shadow Color", &m.shadowColor.x);
        ImGui::SliderFloat("Shadow Threshold", &m.shadowThreshold, 0.0f, 1.0f);
        ImGui::SliderFloat("Shadow Softness", &m.shadowSoftness, 0.001f, 0.5f);
        ImGui::SliderFloat("Ramp Shift", &m.rampShift, -0.5f, 0.5f);
        ImGui::ColorEdit3("Rim Color", &m.rimColor.x);
        ImGui::SliderFloat("Rim Width", &m.rimWidth, 0.01f, 2.0f);
        ImGui::SliderFloat("Rim Intensity", &m.rimIntensity, 0.0f, 3.0f);
        ImGui::ColorEdit3("Spec Color", &m.toonSpecColor.x);
        ImGui::SliderFloat("Spec Size", &m.toonSpecSize, 0.0f, 0.5f);
        ImGui::SliderFloat("Spec Intensity", &m.toonSpecIntensity, 0.0f, 3.0f);
        ImGui::Checkbox("Outline", &m.outline);
        if (m.outline) {
            ImGui::SliderFloat("Outline Width", &m.outlineWidth, 0.0f, 0.01f, "%.4f");
            ImGui::ColorEdit3("Outline Color", &m.outlineColor.x);
        }
    }
    ImGui::SliderFloat("AO", &m.ao, 0.0f, 1.0f);
    ImGui::ColorEdit3("Emissive", &m.emissive.x);
    ImGui::SliderFloat("Emissive Power", &m.emissiveIntensity, 0.0f, 20.0f);
    ImGui::SliderFloat("Normal Strength", &m.normalStrength, 0.0f, 3.0f);
    ImGui::SliderFloat("Alpha Cutoff", &m.alphaCutoff, 0.0f, 1.0f);
    ImGui::Checkbox("Double Sided", &m.doubleSided);

    if (ImGui::TreeNodeEx("Textures", ImGuiTreeNodeFlags_DefaultOpen)) {
        textureSlot("Albedo", m.albedoMap, true);
        textureSlot("Normal", m.normalMap, false);
        textureSlot("Metallic", m.metallicMap, false);
        textureSlot("Roughness", m.roughnessMap, false);
        textureSlot("AO", m.aoMap, false);
        textureSlot("Emissive", m.emissiveMap, true);
        textureSlot("Toon Ramp", m.rampMap, false);
        ImGui::TreePop();
    }

    ImGui::SeparatorText("Apply to whole subtree");
    if (ImGui::Button("Set subtree -> Toon")) {
        node.traverse([](Node& n, const glm::mat4&) {
            if (n.material) {
                n.material->model = ShadingModel::Toon;
                n.material->outline = true;
            }
        });
    }
    ImGui::SameLine();
    if (ImGui::Button("Set subtree -> PBR")) {
        node.traverse([](Node& n, const glm::mat4&) {
            if (n.material) {
                n.material->model = ShadingModel::PBR;
                n.material->outline = false;
            }
        });
    }
}

void EditorUI::drawLights() {
    ImGui::Begin("Lights");

    ImGui::SeparatorText("Sun (Directional)");
    SunLight& sun = scene_->sun;
    ImGui::SliderFloat("Azimuth", &sun.azimuth, 0.0f, 360.0f);
    ImGui::SliderFloat("Elevation", &sun.elevation, -10.0f, 90.0f);
    ImGui::ColorEdit3("Color##sun", &sun.color.x);
    ImGui::DragFloat("Intensity##sun", &sun.intensity, 0.05f, 0.0f, 100.0f);
    ImGui::Checkbox("Cast Shadows", &sun.castShadows);
    ImGui::SliderFloat("Shadow Area", &sun.shadowOrthoSize, 1.0f, 50.0f);
    ImGui::SliderFloat("Shadow Bias", &sun.shadowBias, 0.0f, 0.01f, "%.4f");

    ImGui::SeparatorText("Point Lights");
    ImGui::TextDisabled("%d / %d", (int)scene_->pointLights.size(), kMaxPointLights);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Add") && !scene_->addPointLight())
        Log::warn("Point light limit (%d) reached", kMaxPointLights);

    int removeIdx = -1;
    for (int i = 0; i < (int)scene_->pointLights.size(); ++i) {
        PointLight& l = scene_->pointLights[i];
        ImGui::PushID(i);
        bool open = ImGui::TreeNodeEx(l.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (open) {
            ImGui::Checkbox("Enabled", &l.enabled);
            ImGui::DragFloat3("Position", &l.position.x, 0.05f);
            ImGui::ColorEdit3("Color", &l.color.x);
            ImGui::DragFloat("Intensity", &l.intensity, 0.1f, 0.0f, 1000.0f);
            ImGui::DragFloat("Radius", &l.radius, 0.1f, 0.1f, 100.0f);
            if (ImGui::SmallButton("Remove")) removeIdx = i;
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (removeIdx >= 0)
        scene_->pointLights.erase(scene_->pointLights.begin() + removeIdx);

    ImGui::End();
}

void EditorUI::drawEnvironment() {
    ImGui::Begin("Environment");
    EnvironmentSettings& env = scene_->environment;

    ImGui::TextWrapped("HDR: %s", env.hdrPath.empty() ? "(procedural sky)" : env.hdrPath.c_str());
    if (ImGui::Button("Load .hdr...")) loadHdrDialog();
    ImGui::SameLine();
    if (ImGui::Button("Use Procedural Sky")) {
        env.hdrPath.clear();
        env.dirty = true;
    }
    if (ImGui::SliderFloat("Rotation", &env.rotationDeg, 0.0f, 360.0f)) env.dirty = true;
    ImGui::SliderFloat("IBL Intensity", &env.intensity, 0.0f, 4.0f);
    ImGui::SliderFloat("Background Blur", &env.backgroundLod, 0.0f, 6.0f);
    if (ImGui::Button("Re-bake IBL")) env.dirty = true;
    ImGui::End();
}

void EditorUI::drawPostProcessing() {
    ImGui::Begin("Post FX");
    RendererSettings& rs = renderer_->settings;

    ImGui::SeparatorText("Tonemap");
    ImGui::SliderFloat("Exposure", &rs.exposure, 0.05f, 8.0f);
    int tm = (int)rs.tonemap;
    if (ImGui::Combo("Operator", &tm, "ACES Filmic\0Reinhard\0None (clamp)\0"))
        rs.tonemap = (TonemapMode)tm;

    ImGui::SeparatorText("Bloom");
    Bloom::Settings& b = renderer_->bloom().settings;
    ImGui::Checkbox("Enabled", &b.enabled);
    ImGui::SliderFloat("Threshold", &b.threshold, 0.0f, 4.0f);
    ImGui::SliderFloat("Knee", &b.kneeSoftness, 0.0f, 1.0f);
    ImGui::SliderFloat("Intensity", &b.intensity, 0.0f, 0.5f);
    ImGui::SliderFloat("Radius", &b.radius, 0.25f, 3.0f);

    ImGui::SeparatorText("Debug");
    ImGui::Checkbox("Wireframe", &rs.wireframe);
    ImGui::End();
}

void EditorUI::drawBRDFEditor() {
    ImGui::Begin("BRDF Editor");

    // --- Variant selection for the selected material ---
    auto node = scene_->selected;
    Material* mat = node && node->material ? node->material.get() : nullptr;
    if (mat) {
        ImGui::Text("Material: %s", mat->name.c_str());
        int ndf = (int)mat->ndf, geom = (int)mat->geom, fres = (int)mat->fresnel;
        if (ImGui::Combo("NDF (D)", &ndf, "GGX / Trowbridge-Reitz\0Beckmann\0Blinn-Phong\0"))
            mat->ndf = (NDFType)ndf;
        if (ImGui::Combo("Geometry (G)", &geom,
                         "Smith height-correlated GGX\0Smith Schlick-GGX\0Implicit\0"))
            mat->geom = (GeomType)geom;
        if (ImGui::Combo("Fresnel (F)", &fres, "Schlick\0Schlick + Roughness\0None (F0)\0"))
            mat->fresnel = (FresnelType)fres;
        ImGui::SliderFloat("Specular Tint", &mat->specularTint, 0.0f, 1.0f);
        ImGui::SliderFloat("Energy Comp.", &mat->energyCompensation, 0.5f, 2.0f);
    } else {
        ImGui::TextDisabled("Select a node with a material to edit BRDF variants.");
    }

    ImGui::SeparatorText("brdf.glsl (hot-reloaded)");

    if (!brdfLoaded_) {
        std::ifstream f(brdfPath_, std::ios::binary);
        if (f) {
            std::string src((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            size_t n = std::min(src.size(), brdfBuffer_.size() - 1);
            std::memcpy(brdfBuffer_.data(), src.data(), n);
            brdfBuffer_[n] = 0;
        }
        brdfLoaded_ = true;
    }

    if (ImGui::Button("Save && Recompile")) {
        std::ofstream f(brdfPath_, std::ios::binary);
        f.write(brdfBuffer_.data(), (std::streamsize)std::strlen(brdfBuffer_.data()));
        Log::info("Saved %s", brdfPath_.c_str());
        // FileWatcher picks up the change and hot-reloads the PBR shader.
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert")) brdfLoaded_ = false;

    auto pbr = shaders_->get("pbr");
    if (pbr && !pbr->lastError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("%s", pbr->lastError().c_str());
        ImGui::PopStyleColor();
    }

    ImGui::InputTextMultiline("##brdfsrc", brdfBuffer_.data(), brdfBuffer_.size(),
                              ImVec2(-1, -1), ImGuiInputTextFlags_AllowTabInput);
    ImGui::End();
}

// ---------------------------------------------------------- Frame Debugger
void EditorUI::drawResourceThumb(const DebugResource& res, int passIdx, bool isInput, int ioIdx) {
    ImGui::PushID((passIdx << 16) | (isInput ? 0x8000 : 0) | ioIdx);
    ImGui::BeginGroup();

    const float thumbSize = 84.0f;
    bool selected = hasSelection_ && selResource_.tex == res.tex && selLabel_ == res.label;
    if (selected)
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.4f, 0.7f, 1.0f, 1.0f));

    bool clicked = false;
    if (res.target == GL_TEXTURE_CUBE_MAP) {
        clicked = ImGui::Button("Cubemap\n(click to\ninspect)", ImVec2(thumbSize, thumbSize));
    } else {
        clicked = ImGui::ImageButton("##thumb", toImTex(res.tex), ImVec2(thumbSize, thumbSize),
                                     ImVec2(0, 1), ImVec2(1, 0));
    }
    if (selected) ImGui::PopStyleColor();

    if (clicked) {
        hasSelection_ = true;
        selResource_ = res;
        selLabel_ = res.label;
        viewParams_.cubeFace = 0;
        viewParams_.mip = 0;
        if (res.isDepth) {
            viewParams_.channel = 1;
            viewParams_.gamma = false;
        }
    }
    ImGui::TextWrapped("%s", res.label.c_str());
    ImGui::TextDisabled("%dx%d", res.width, res.height);
    ImGui::EndGroup();
    ImGui::PopID();
}

void EditorUI::drawFrameDebugger() {
    ImGui::Begin("Frame Debugger");

    ImGui::TextDisabled("Passes this frame (like RenderDoc's event browser):");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 130);
    ImGui::Text("GPU total: %.2f ms", renderer_->frameDebugger().totalGpuMs());
    ImGui::Separator();

    ImGui::BeginChild("passlist", ImVec2(280, 0), ImGuiChildFlags_ResizeX);
    static int selPassIdx = -1;
    const auto& passes = renderer_->frameDebugger().passes();
    for (int i = 0; i < (int)passes.size(); ++i) {
        const PassRecord& p = passes[i];
        char label[160];
        snprintf(label, sizeof(label), "%s  [%.2f ms, %d draws]", p.name.c_str(), p.gpuMs,
                 p.drawCalls);
        if (ImGui::Selectable(label, selPassIdx == i)) selPassIdx = i;
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("passdetail");
    if (selPassIdx >= 0 && selPassIdx < (int)passes.size()) {
        const PassRecord& p = passes[selPassIdx];
        ImGui::SeparatorText(p.name.c_str());
        ImGui::TextWrapped("%s", p.description.c_str());
        ImGui::Text("GPU: %.3f ms   Draw calls: %d", p.gpuMs, p.drawCalls);

        ImGui::SeparatorText("Inputs");
        if (p.inputs.empty()) ImGui::TextDisabled("(none)");
        for (int i = 0; i < (int)p.inputs.size(); ++i) {
            drawResourceThumb(p.inputs[i], selPassIdx, true, i);
            if (i + 1 < (int)p.inputs.size()) ImGui::SameLine();
        }

        ImGui::SeparatorText("Outputs");
        if (p.outputs.empty()) ImGui::TextDisabled("(none)");
        for (int i = 0; i < (int)p.outputs.size(); ++i) {
            drawResourceThumb(p.outputs[i], selPassIdx, false, i);
            if (i + 1 < (int)p.outputs.size()) ImGui::SameLine();
        }

        drawTextureInspector();
    } else {
        ImGui::TextDisabled("Select a pass on the left to see its inputs / outputs.");
    }
    ImGui::EndChild();
    ImGui::End();
}

void EditorUI::drawTextureInspector() {
    if (!hasSelection_) return;
    ImGui::SeparatorText(("Inspector: " + selLabel_).c_str());

    ImGui::SetNextItemWidth(140);
    ImGui::Combo("Channel", &viewParams_.channel, "RGB\0R\0G\0B\0A\0");
    ImGui::SameLine();
    ImGui::Checkbox("Gamma", &viewParams_.gamma);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220);
    float range[2] = {viewParams_.rangeMin, viewParams_.rangeMax};
    if (ImGui::DragFloat2("Range", range, 0.01f)) {
        viewParams_.rangeMin = range[0];
        viewParams_.rangeMax = range[1];
    }
    if (selResource_.target == GL_TEXTURE_CUBE_MAP) {
        ImGui::SetNextItemWidth(140);
        ImGui::Combo("Face", &viewParams_.cubeFace, "+X\0-X\0+Y\0-Y\0+Z\0-Z\0");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120);
        ImGui::SliderFloat("Mip", &viewParams_.mip, 0.0f, 8.0f);
    }

    GLuint view = renderer_->debugView(selResource_, viewParams_);
    if (view) {
        float availW = ImGui::GetContentRegionAvail().x;
        float aspect = selResource_.height > 0
                           ? (float)selResource_.height / (float)selResource_.width : 1.0f;
        float w = std::min(availW, 640.0f);
        ImGui::Image(toImTex(view), ImVec2(w, w * aspect));
    }
}

void EditorUI::drawConsole() {
    ImGui::Begin("Console");
    if (ImGui::SmallButton("Clear")) Log::clear();
    ImGui::Separator();
    ImGui::BeginChild("log", ImVec2(0, 0), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& e : Log::snapshot()) {
        ImVec4 col = e.level == LogLevel::Error   ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f)
                     : e.level == LogLevel::Warn ? ImVec4(1.0f, 0.85f, 0.5f, 1.0f)
                                                 : ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_Text, col);
        ImGui::TextUnformatted(e.text.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::End();
}

// ------------------------------------------------------------------- misc
void EditorUI::importModelDialog() {
    std::string p = openFileDialog(
        "Models\0*.fbx;*.obj;*.gltf;*.glb;*.dae;*.pmx\0All\0*.*\0");
    if (!p.empty()) importModel(p);
}

void EditorUI::importModel(const std::string& path) {
    auto node = ModelLoader::load(path);
    if (node) {
        scene_->root->addChild(node);
        scene_->selected = node;
    }
}

void EditorUI::loadHdrDialog() {
    std::string p = openFileDialog("HDR\0*.hdr\0All\0*.*\0");
    if (!p.empty()) {
        scene_->environment.hdrPath = p;
        scene_->environment.dirty = true;
    }
}
