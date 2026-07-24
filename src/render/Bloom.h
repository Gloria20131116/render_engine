#pragma once
#include <GL/glew.h>

#include <memory>
#include <vector>

class ShaderLibrary;
class Texture;
class FrameDebugger;

// Physically-based bloom: progressive downsample (13-tap, Karis average on the
// first step) followed by tent-filter upsampling. Result is composited in the
// tonemap pass.
class Bloom {
public:
    struct Settings {
        bool enabled = true;
        float threshold = 1.0f;    // soft knee threshold applied on prefilter
        float kneeSoftness = 0.5f;
        float intensity = 0.06f;   // mix factor in tonemap
        float radius = 1.0f;       // upsample filter radius scale
    };

    void init(ShaderLibrary& shaders);
    void resize(int width, int height);
    // Runs the bloom chain on sceneColor; result available via result().
    void render(GLuint sceneColor, FrameDebugger& dbg);

    GLuint result() const { return mips_.empty() ? 0 : mips_[0].tex; }
    Settings settings;

private:
    struct Mip {
        GLuint tex = 0;
        int w = 0, h = 0;
    };

    ShaderLibrary* shaders_ = nullptr;
    std::vector<Mip> mips_;
    GLuint fbo_ = 0;
    int width_ = 0, height_ = 0;

    friend class EditorUI;
    const std::vector<Mip>& mipChain() const { return mips_; }
};
