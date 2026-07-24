#pragma once
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <memory>

#include "material/ShaderCache.h"
#include "render/Bloom.h"
#include "render/FrameDebug.h"
#include "render/Framebuffer.h"
#include "render/IBL.h"

class Scene;
class Node;
class ShaderLibrary;
struct Material;
class Shader;
class Texture;

enum class TonemapMode : int { ACES = 0, Reinhard = 1, None = 2 };

struct RendererSettings {
    float exposure = 1.0f;
    TonemapMode tonemap = TonemapMode::ACES;
    bool drawGroundPlane = true;
    bool wireframe = false;
    int shadowMapSize = 2048;
    int msaaSamples = 4;        // 1 = off; 2/4/8 = MSAA on the scene target
    bool outlineEnabled = true; // global toggle for the outline pass
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
    // Blended materials, drawn after the skybox sorted back-to-front with
    // depth test on but depth writes off.
    void transparentPass(Scene& scene);
    void outlinePass(Scene& scene);
    void skyboxPass(Scene& scene);
    void resolveMsaa();
    void tonemapPass();
    void bindMaterial(Shader& sh, const Material& mat);
    // Draws one node, dispatching to its graph-generated shader when it has a
    // material graph, else to the unified `pbr` shader (`sh`, already bound).
    // Applies the material's custom depth state (test/write/bias); the default
    // depth-write behaviour depends on which pass is drawing.
    void drawWithMaterial(Shader& sh, Scene& scene, Node& node, const glm::mat4& world,
                          PassRecord& rec, bool inTransparentPass);
    // Camera/sun/point-light/IBL/shadow uniforms shared by the unified pbr
    // shader and every graph-generated shader. `iblBaseUnit` is the first
    // texture unit for IBL+shadow maps (graph shaders keep 0..7 for graph
    // textures, so they use a higher base).
    void bindLighting(Shader& sh, Scene& scene, int iblBaseUnit);
    void bindGraphMaterial(Shader& sh, const Material& mat);
    glm::mat4 sunLightMatrix(const Scene& scene) const;

    // Scene passes render into the MSAA target when enabled, else sceneFbo_.
    bool msaaActive() const { return settings.msaaSamples > 1; }
    void bindSceneTarget();
    void ensureMsaaTarget();

    ShaderLibrary* shaders_ = nullptr;
    FrameDebugger debugger_;
    IBL ibl_;
    Bloom bloom_;

    Framebuffer shadowFbo_;   // depth only
    Framebuffer sceneFbo_;    // RGBA16F + depth (resolve target when MSAA on)
    Framebuffer finalFbo_;    // RGBA8 (after tonemap)
    Framebuffer inspectFbo_;  // RGBA8 (texture inspector)

    // Multisampled scene target (raw GL, resolved into sceneFbo_).
    GLuint msaaFbo_ = 0, msaaColor_ = 0, msaaDepth_ = 0;
    int msaaW_ = 0, msaaH_ = 0, msaaSamples_ = 0;

    GraphShaderCache graphCache_;
    std::shared_ptr<Texture> whiteTex_;  // fallback for unbound graph samplers

    glm::mat4 lightMatrix_{1.0f};
    glm::mat4 view_{1.0f}, proj_{1.0f};
    glm::vec3 cameraPos_{0.0f};
    float time_ = 0.0f;
    int vpWidth_ = 1280, vpHeight_ = 720;
    int mainPassDrawCalls_ = 0;
};
