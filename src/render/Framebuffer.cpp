#include "render/Framebuffer.h"

#include "core/Log.h"

void Framebuffer::create(int w, int h, const std::vector<Attachment>& colors, bool depthTexture,
                         const std::string& name) {
    destroy();
    w_ = w;
    h_ = h;
    colors_ = colors;
    useDepth_ = depthTexture;
    name_ = name;
    build();
}

void Framebuffer::resize(int w, int h) {
    if (w == w_ && h == h_) return;
    w_ = w;
    h_ = h;
    build();
}

void Framebuffer::build() {
    // Delete previous GL objects but keep attachment descriptions.
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    for (auto& c : colors_)
        if (c.tex) glDeleteTextures(1, &c.tex);
    if (depthTex_) glDeleteTextures(1, &depthTex_);
    depthTex_ = 0;

    glGenFramebuffers(1, &fbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    std::vector<GLenum> drawBuffers;
    for (size_t i = 0; i < colors_.size(); ++i) {
        auto& c = colors_[i];
        glGenTextures(1, &c.tex);
        glBindTexture(GL_TEXTURE_2D, c.tex);
        glTexStorage2D(GL_TEXTURE_2D, 1, c.internalFormat, w_, h_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + (GLenum)i, GL_TEXTURE_2D,
                               c.tex, 0);
        drawBuffers.push_back(GL_COLOR_ATTACHMENT0 + (GLenum)i);
    }
    if (!drawBuffers.empty())
        glDrawBuffers((GLsizei)drawBuffers.size(), drawBuffers.data());
    else {
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
    }

    if (useDepth_) {
        glGenTextures(1, &depthTex_);
        glBindTexture(GL_TEXTURE_2D, depthTex_);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT32F, w_, h_);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[4] = {1, 1, 1, 1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTex_, 0);
    }

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        Log::error("Framebuffer '%s' incomplete", name_.c_str());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::destroy() {
    if (fbo_) glDeleteFramebuffers(1, &fbo_);
    for (auto& c : colors_)
        if (c.tex) glDeleteTextures(1, &c.tex);
    if (depthTex_) glDeleteTextures(1, &depthTex_);
    fbo_ = 0;
    depthTex_ = 0;
    for (auto& c : colors_) c.tex = 0;
}

void Framebuffer::bind() const {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, w_, h_);
}

void Framebuffer::unbind() { glBindFramebuffer(GL_FRAMEBUFFER, 0); }
