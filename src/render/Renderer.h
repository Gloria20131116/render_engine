#pragma once
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <memory>

#include "render/Bloom.h"
#include "render/FrameDebug.h"
#include "render/Framebuffer.h"
#include "render/IBL.h"

class Scene;
class ShaderLibrary;
struct Material;
class Shader;

enum class TonemapMode : int { ACES = 0, Reinhard = 1, None = 2 };

struct RendererSettings {
    float exposure = 1.0f;
    TonemapMode tonemap = TonemapMode::ACES;
    bool drawGroundPlane = true;
    bool wireframe = false;
    int shadowMapSize = 2048;
};

// Parameters for the RenderDoc-style texture inspector.
struct DebugViewParams {
    int channel = 0;      // 0=RGB 1=R 2=G 3=B 4=A
    float rangeMin = 0.0f;
    float rangeMax = 1.0f;
    int cubeFace = 0;     // for cubemaps
    float mip = 0.0f;
    bool gamma = true;
    bool flipY = false;
};

class Renderer {
public:
    void init(ShaderLibrary& shaders);
    void resizeViewport(int w, int h);
    void render(Scene& scene);

    // Renders `tex` through the inspector shader (channel select / range remap)
    // into an internal RGBA8 target and returns it for ImGui display.
    GLuint debugView(const DebugResource& res, const DebugViewParams& params);

    RendererSettings settings;
    Bloom& bloom() { return bloom_; }
    IBL& ibl() { return ibl_; }
    FrameDebugger& frameDebugger() { return debugger_; }

    GLuint finalTexture() const { return finalFbo_.colorTex(); }
    int viewportWidth() const { return vpWidth_; }
    int viewportHeight() const { return vpHeight_; }

private:
    void shadowPass(Scene& scene);
    void mainPass(Scene& scene);
    void skyboxPass(Scene& scene);
    void tonemapPass();
    void bindMaterial(Shader& sh, const Material& mat);
    glm::mat4 sunLightMatrix(const Scene& scene) const;

    ShaderLibrary* shaders_ = nullptr;
    FrameDebugger debugger_;
    IBL ibl_;
    Bloom bloom_;

    Framebuffer shadowFbo_;   // depth only
    Framebuffer sceneFbo_;    // RGBA16F + depth
    Framebuffer finalFbo_;    // RGBA8 (after tonemap)
    Framebuffer inspectFbo_;  // RGBA8 (texture inspector)

    glm::mat4 lightMatrix_{1.0f};
    glm::mat4 view_{1.0f}, proj_{1.0f};
    glm::vec3 cameraPos_{0.0f};
    int vpWidth_ = 1280, vpHeight_ = 720;
    int mainPassDrawCalls_ = 0;
};
