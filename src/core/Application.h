#pragma once
#include "core/EventBus.h"
#include "core/FileWatcher.h"
#include "core/Window.h"
#include "render/Renderer.h"
#include "render/ShaderLibrary.h"
#include "scene/Scene.h"
#include "ui/EditorUI.h"

class Application {
public:
    bool init();
    void run();
    void shutdown();

    // Automation: after `frames` frames, save the window to `path` and quit.
    void requestScreenshot(const std::string& path, int frames) {
        screenshotPath_ = path;
        screenshotFrame_ = frames;
    }

private:
    void buildDefaultScene();
    void onFileDropped(const std::string& path);
    void saveScreenshot();

    std::string screenshotPath_;
    int screenshotFrame_ = -1;
    int frameIndex_ = 0;

    EventBus bus_;
    Window window_;
    FileWatcher watcher_{bus_};
    ShaderLibrary shaders_;
    Scene scene_;
    Renderer renderer_;
    EditorUI ui_;
};
