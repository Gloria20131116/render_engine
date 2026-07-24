#include "core/Application.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include "core/Events.h"
#include "core/Log.h"
#include "core/Paths.h"
#include "scene/Material.h"
#include "scene/Mesh.h"

bool Application::init() {
    if (!window_.init(1600, 900, "Render Engine - PBR / NPR LookDev", bus_)) return false;

    Log::info("Assets directory: %s", Paths::assets().string().c_str());

    shaders_.init(bus_, watcher_);
    renderer_.init(shaders_);
    ui_.init(window_, scene_, renderer_, shaders_, bus_);

    buildDefaultScene();

    bus_.subscribe<FileDroppedEvent>(
        [this](const FileDroppedEvent& e) { onFileDropped(e.path); });

    return true;
}

void Application::buildDefaultScene() {
    scene_.environment.dirty = true;  // bake procedural sky IBL on first frame

    // Ground
    auto ground = scene_.root->addChild("Ground");
    ground->mesh = Mesh::plane(30.0f);
    ground->material = std::make_shared<Material>();
    ground->material->name = "Ground";
    ground->material->baseColor = {0.35f, 0.35f, 0.37f};
    ground->material->roughness = 0.9f;

    // Roughness/metallic study spheres (classic lookdev grid)
    auto grid = scene_.root->addChild("Material Grid");
    grid->position = {0.0f, 0.6f, -2.0f};
    const int cols = 6;
    for (int row = 0; row < 2; ++row) {
        for (int c = 0; c < cols; ++c) {
            auto s = grid->addChild("Sphere R" + std::to_string(row) + "C" + std::to_string(c));
            s->mesh = Mesh::sphere(0.42f, 48, 32);
            s->position = {(c - (cols - 1) * 0.5f) * 1.0f, row * 1.0f, 0.0f};
            auto m = std::make_shared<Material>();
            m->name = (row == 0 ? "Dielectric r=" : "Metal r=") +
                      std::to_string((int)(100.0f * c / (cols - 1)));
            m->baseColor = row == 0 ? glm::vec3(0.9f, 0.25f, 0.2f) : glm::vec3(0.95f, 0.78f, 0.34f);
            m->metallic = row == 0 ? 0.0f : 1.0f;
            m->roughness = std::max(0.05f, (float)c / (cols - 1));
            s->material = m;
        }
    }

    // Toon demo sphere (ZZZ / Endfield style stand-in until a model is imported)
    auto toon = scene_.root->addChild("Toon Demo");
    toon->position = {0.0f, 1.0f, 1.2f};
    toon->mesh = Mesh::sphere(0.6f, 64, 48);
    auto toonMat = std::make_shared<Material>();
    toonMat->name = "Toon Preview";
    toonMat->model = ShadingModel::Toon;
    toonMat->baseColor = {0.93f, 0.78f, 0.75f};
    toonMat->outline = true;
    toon->material = toonMat;

    // One warm fill light as an example (up to 5 supported)
    scene_.addPointLight();
    scene_.pointLights[0].name = "Warm Fill";
    scene_.pointLights[0].position = {-2.5f, 2.5f, 2.0f};
    scene_.pointLights[0].color = {1.0f, 0.6f, 0.35f};
    scene_.pointLights[0].intensity = 6.0f;

    scene_.camera.target = {0.0f, 1.0f, 0.0f};
    scene_.camera.distance = 6.0f;
    scene_.selected = toon;
}

void Application::onFileDropped(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    if (ext == ".fbx" || ext == ".obj" || ext == ".gltf" || ext == ".glb" || ext == ".dae" ||
        ext == ".pmx") {
        ui_.importModel(path);
    } else if (ext == ".hdr") {
        scene_.environment.hdrPath = path;
        scene_.environment.dirty = true;
    } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".tga" ||
               ext == ".bmp") {
        if (scene_.selected && scene_.selected->material) {
            auto t = Texture::load2D(path, true);
            if (t) scene_.selected->material->albedoMap = t;
        } else {
            Log::warn("Drop a texture with a node selected to assign it as albedo");
        }
    } else {
        Log::warn("Unsupported file dropped: %s", path.c_str());
    }
}

void Application::run() {
    double lastTime = glfwGetTime();
    while (!window_.shouldClose()) {
        window_.pollEvents();

        double now = glfwGetTime();
        float dt = (float)(now - lastTime);
        lastTime = now;

        watcher_.update(dt);

        // Render the scene at the size the viewport panel had last frame.
        renderer_.resizeViewport(ui_.desiredViewportWidth(), ui_.desiredViewportHeight());
        renderer_.render(scene_);

        // Editor UI on the default framebuffer.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, window_.width(), window_.height());
        glClearColor(0.05f, 0.05f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ui_.beginFrame();
        ui_.draw(dt);
        ui_.endFrame();

        frameIndex_++;
        if (screenshotFrame_ > 0 && frameIndex_ >= screenshotFrame_) {
            saveScreenshot();
            glfwSetWindowShouldClose(window_.handle(), 1);
        }

        window_.swapBuffers();
    }
}

void Application::saveScreenshot() {
    for (const auto& p : renderer_.frameDebugger().passes())
        Log::info("Pass '%s': %.3f ms, %d draws", p.name.c_str(), p.gpuMs, p.drawCalls);

    int w = window_.width(), h = window_.height();
    std::vector<unsigned char> pixels(w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glReadBuffer(GL_BACK);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    stbi_write_png(screenshotPath_.c_str(), w, h, 4, pixels.data(), w * 4);
    Log::info("Screenshot saved: %s", screenshotPath_.c_str());
}

void Application::shutdown() {
    ui_.shutdown();
    window_.shutdown();
}
