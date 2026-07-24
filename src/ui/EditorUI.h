#pragma once
#include <memory>
#include <string>
#include <vector>
#include <vector>

#include "render/Renderer.h"
#include "ui/NodeEditor.h"

class Scene;
class ShaderLibrary;
class Window;
class EventBus;
class Node;

// All editor panels: viewport, hierarchy, inspector, lights, environment,
// post-processing, BRDF editor, frame debugger (RenderDoc-style), console.
class EditorUI {
public:
    void init(Window& window, Scene& scene, Renderer& renderer, ShaderLibrary& shaders,
              EventBus& bus);
    void shutdown();

    void beginFrame();
    void draw(float dt);
    void endFrame();

    // Size the viewport panel requested last frame.
    int desiredViewportWidth() const { return vpDesiredW_; }
    int desiredViewportHeight() const { return vpDesiredH_; }

    void importModelDialog();
    void importModel(const std::string& path);
    void loadHdrDialog();

private:
    void setupStyle();
    void buildDefaultLayout(unsigned dockspaceId);
    void drawMenuBar();
    void drawViewport();
    void drawHierarchy();
    void drawNodeTree(const std::shared_ptr<Node>& node);
    void drawInspector();
    void drawMaterialEditor(Node& node);
    void drawLights();
    void drawEnvironment();
    void drawPostProcessing();
    void drawBRDFEditor();
    void drawFrameDebugger();
    void drawResourceThumb(const DebugResource& res, int passIdx, bool isInput, int ioIdx);
    void drawTextureInspector();
    void drawConsole();

    Window* window_ = nullptr;
    Scene* scene_ = nullptr;
    Renderer* renderer_ = nullptr;
    ShaderLibrary* shaders_ = nullptr;
    EventBus* bus_ = nullptr;

    int vpDesiredW_ = 1280, vpDesiredH_ = 720;
    float dt_ = 0.0f;
    bool layoutInitialized_ = false;
    int focusGraphFrames_ = 0;  // select the Material Graph tab after layout build

    // Frame debugger selection
    bool hasSelection_ = false;
    std::string selPass_, selLabel_;
    DebugResource selResource_{};
    DebugViewParams viewParams_;

    // BRDF editor
    std::string brdfPath_;
    std::vector<char> brdfBuffer_;
    bool brdfLoaded_ = false;

    // Material graph editor
    NodeEditor nodeEditor_;
};
