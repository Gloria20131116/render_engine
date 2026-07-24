#include "render/Bloom.h"

#include <algorithm>
#include <glm/glm.hpp>

#include "render/FrameDebug.h"
#include "render/RenderUtil.h"
#include "render/ShaderLibrary.h"

void Bloom::init(ShaderLibrary& shaders) {
    shaders_ = &shaders;
    shaders.load("bloom_downsample", "fullscreen.vert", "bloom_downsample.frag");
    shaders.load("bloom_upsample", "fullscreen.vert", "bloom_upsample.frag");
    glGenFramebuffers(1, &fbo_);
}

void Bloom::resize(int width, int height) {
    if (width == width_ && height == height_) return;
    width_ = width;
    height_ = height;

    for (auto& m : mips_)
        if (m.tex) glDeleteTextures(1, &m.tex);
    mips_.clear();

    int w = width / 2, h = height / 2;
    const int maxMips = 6;
    for (int i = 0; i < maxMips && w >= 8 && h >= 8; ++i) {
        Mip m;
        m.w = w;
        m.h = h;
        glGenTextures(1, &m.tex);
        glBindTexture(GL_TEXTURE_2D, m.tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA16F, w, h);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        mips_.push_back(m);
        w /= 2;
        h /= 2;
    }
}

void Bloom::render(GLuint sceneColor, FrameDebugger& dbg) {
    if (mips_.empty() || !settings.enabled) return;

    PassRecord& rec = dbg.beginPass("Bloom", "Progressive downsample + tent upsample chain");
    rec.inputs.push_back({"Scene Color (HDR)", sceneColor, GL_TEXTURE_2D, width_, height_});

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);

    // ---- Downsample chain ----
    auto down = shaders_->get("bloom_downsample");
    down->bind();
    down->setInt("uSource", 0);
    GLuint src = sceneColor;
    int srcW = width_, srcH = height_;
    for (size_t i = 0; i < mips_.size(); ++i) {
        down->setVec2("uSrcTexelSize", {1.0f / srcW, 1.0f / srcH});
        down->setInt("uFirstPass", i == 0 ? 1 : 0);
        down->setFloat("uThreshold", settings.threshold);
        down->setFloat("uKnee", settings.kneeSoftness);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, src);
        glViewport(0, 0, mips_[i].w, mips_[i].h);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mips_[i].tex, 0);
        RenderUtil::drawFullscreen();
        rec.drawCalls++;
        src = mips_[i].tex;
        srcW = mips_[i].w;
        srcH = mips_[i].h;
    }

    // ---- Upsample chain (additive blend into the next-larger mip) ----
    auto up = shaders_->get("bloom_upsample");
    up->bind();
    up->setInt("uSource", 0);
    up->setFloat("uRadius", settings.radius);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    for (int i = (int)mips_.size() - 1; i > 0; --i) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, mips_[i].tex);
        up->setVec2("uSrcTexelSize", {1.0f / mips_[i].w, 1.0f / mips_[i].h});
        glViewport(0, 0, mips_[i - 1].w, mips_[i - 1].h);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               mips_[i - 1].tex, 0);
        RenderUtil::drawFullscreen();
        rec.drawCalls++;
    }
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    for (size_t i = 0; i < mips_.size(); ++i)
        rec.outputs.push_back({"Bloom Mip " + std::to_string(i), mips_[i].tex, GL_TEXTURE_2D,
                               mips_[i].w, mips_[i].h});
    dbg.endPass("Bloom");
}
