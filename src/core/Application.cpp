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
#include "material/MaterialGraph.h"
#include "scene/Material.h"
#include "scene/Mesh.h"
#include "scene/SceneProject.h"

bool Application::init() {
    if (!window_.init(1600, 900, "Render Engine - PBR / NPR LookDev", bus_)) return false;

    Log::info("Assets directory: %s", Paths::assets().string().c_str());

    shaders_.init(bus_, watcher_);
    renderer_.init(shaders_);
    ui_.init(window_, scene_, renderer_, shaders_, bus_);

    // Restore the last saved/opened project; fall back to the default scene.
    std::string lastProject = SceneProject::readLastProjectPath();
    if (lastProject.empty() || !std::filesystem::exists(lastProject) ||
        !SceneProject::load(lastProject, scene_, renderer_)) {
        buildDefaultScene();
    }

    bus_.subscribe<FileDroppedEvent>(
        [this](const FileDroppedEvent& e) { onFileDropped(e.path); });

    if (!importOnStart_.empty()) {
        Log::info("CLI import: %s", importOnStart_.c_str());
        ui_.importModel(importOnStart_);
    }

    return true;
}

void Application::buildDefaultScene() {
    scene_.environment.dirty = true;  // bake procedural sky IBL on first frame

    // Ground
    auto ground = scene_.root->addChild("Ground");
    ground->mesh = Mesh::plane(30.0f);
    ground->primitive = PrimitiveKind::Plane;
    ground->primitiveParams = {30.0f, 0, 0};
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
            s->primitive = PrimitiveKind::Sphere;
            s->primitiveParams = {0.42f, 48, 32};
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

    // Principled multi-lobe demo row: one sphere per shading model so the new
    // surface types are visible (and switchable in the Inspector) on first launch.
    {
        auto row = scene_.root->addChild("Principled Demo");
        row->position = {0.0f, 0.55f, 2.6f};
        struct Demo {
            const char* name;
            ShadingModel model;
            glm::vec3 color;
            void (*tweak)(Material&);
        };
        const Demo demos[] = {
            {"Principled", ShadingModel::Principled, {0.85f, 0.30f, 0.25f},
             [](Material& m) { m.roughness = 0.35f; m.sheen = 0.3f; m.anisotropic = 0.4f; }},
            {"Clearcoat", ShadingModel::Clearcoat, {0.15f, 0.25f, 0.75f},
             [](Material& m) { m.metallic = 0.3f; m.roughness = 0.4f; m.clearcoat = 1.0f;
                               m.clearcoatGloss = 0.95f; }},
            {"Cloth", ShadingModel::Cloth, {0.55f, 0.35f, 0.55f},
             [](Material& m) { m.roughness = 0.85f; m.sheen = 0.9f; m.sheenTint = 0.6f; }},
            {"Subsurface", ShadingModel::Subsurface, {0.87f, 0.68f, 0.58f},
             [](Material& m) { m.roughness = 0.55f; m.subsurface = 0.8f; }},
            {"Glass", ShadingModel::Glass, {0.90f, 0.95f, 1.00f},
             [](Material& m) { m.roughness = 0.05f; m.transmission = 0.9f; m.clearcoat = 0.3f;
                               m.blend = BlendMode::Transparent; m.opacity = 0.35f; }},
        };
        const int n = (int)(sizeof(demos) / sizeof(demos[0]));
        for (int i = 0; i < n; ++i) {
            auto s = row->addChild(demos[i].name);
            s->mesh = Mesh::sphere(0.42f, 48, 32);
            s->primitive = PrimitiveKind::Sphere;
            s->primitiveParams = {0.42f, 48, 32};
            s->position = {(i - (n - 1) * 0.5f) * 1.0f, 0.0f, 0.0f};
            auto m = std::make_shared<Material>();
            m->name = demos[i].name;
            m->model = demos[i].model;
            m->baseColor = demos[i].color;
            demos[i].tweak(*m);
            s->material = m;
        }
    }

    // Toon demo sphere (ZZZ / Endfield style stand-in until a model is imported)
    auto toon = scene_.root->addChild("Toon Demo");
    toon->position = {0.0f, 1.0f, 1.2f};
    toon->mesh = Mesh::sphere(0.6f, 64, 48);
    toon->primitive = PrimitiveKind::Sphere;
    toon->primitiveParams = {0.6f, 64, 48};
    auto toonMat = std::make_shared<Material>();
    toonMat->name = "Toon Preview";
    toonMat->model = ShadingModel::Toon;
    toonMat->baseColor = {0.93f, 0.78f, 0.75f};
    toonMat->outline = true;
    toon->material = toonMat;

    // Graph material demo sphere: Fresnel-driven two-color lerp on Base Color,
    // Voronoi noise on Roughness. Open the "Material Graph" panel to edit it.
    auto graphDemo = scene_.root->addChild("Graph Demo");
    graphDemo->position = {2.2f, 1.0f, 1.2f};
    graphDemo->mesh = Mesh::sphere(0.6f, 64, 48);
    graphDemo->primitive = PrimitiveKind::Sphere;
    graphDemo->primitiveParams = {0.6f, 64, 48};
    auto graphMat = std::make_shared<Material>();
    graphMat->name = "Graph Preview";
    graphMat->graph = std::make_shared<MaterialGraph>();
    {
        // NOTE: addNode() may reallocate the node vector, so grab pin ids
        // right away instead of holding GraphNode references across calls.
        MaterialGraph& g = *graphMat->graph;
        int colAOut, colBOut, fresOut, lerpOut, noiseOut;
        int lerpInA, lerpInB, lerpInT;
        {
            GraphNode& n = g.addNode("Color", {-60.0f, 20.0f});
            n.value = {0.85f, 0.15f, 0.35f, 1.0f};
            colAOut = n.outputs[0].id;
        }
        {
            GraphNode& n = g.addNode("Color", {-60.0f, 140.0f});
            n.value = {0.15f, 0.5f, 0.95f, 1.0f};
            colBOut = n.outputs[0].id;
        }
        {
            GraphNode& n = g.addNode("Fresnel", {-60.0f, 260.0f});
            fresOut = n.outputs[0].id;
        }
        {
            GraphNode& n = g.addNode("Lerp", {180.0f, 80.0f});
            lerpInA = n.inputs[0].id;
            lerpInB = n.inputs[1].id;
            lerpInT = n.inputs[2].id;
            lerpOut = n.outputs[0].id;
        }
        {
            GraphNode& n = g.addNode("VoronoiNoise", {180.0f, 280.0f});
            noiseOut = n.outputs[0].id;
        }
        int customOut, customInA;
        {
            // UE-style Custom node demo: time-pulsed emissive glow.
            GraphNode& n = g.addNode("Custom", {180.0f, 420.0f});
            n.customCode = "// Pulsing glow (HLSL aliases work: lerp/saturate/float3)\n"
                           "return a * (0.3 + 0.3 * sin(uTime * 2.0));";
            customInA = n.inputs[0].id;
            customOut = n.outputs[0].id;
        }
        GraphNode* out = g.outputNode();
        g.addLink(colAOut, lerpInA);
        g.addLink(colBOut, lerpInB);
        g.addLink(fresOut, lerpInT);
        g.addLink(colBOut, customInA);
        g.addLink(lerpOut, out->inputs[0].id);    // Base Color
        g.addLink(noiseOut, out->inputs[2].id);   // Roughness
        g.addLink(customOut, out->inputs[4].id);  // Emissive
    }
    graphDemo->material = graphMat;

    // One warm fill light as an example (up to 5 supported)
    scene_.addPointLight();
    scene_.pointLights[0].name = "Warm Fill";
    scene_.pointLights[0].position = {-2.5f, 2.5f, 2.0f};
    scene_.pointLights[0].color = {1.0f, 0.6f, 0.35f};
    scene_.pointLights[0].intensity = 6.0f;

    scene_.camera.target = {0.0f, 1.0f, 0.0f};
    scene_.camera.distance = 6.0f;
    scene_.selected = graphDemo;  // opens the Material Graph demo in the editor
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
