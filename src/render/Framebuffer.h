#pragma once
#include <GL/glew.h>

#include <string>
#include <vector>

// Framebuffer with color attachments + optional depth. Resizable.
class Framebuffer {
public:
    struct Attachment {
        GLenum internalFormat;
        std::string name;
        GLuint tex = 0;
    };

    ~Framebuffer() { destroy(); }

    void create(int w, int h, const std::vector<Attachment>& colors, bool depthTexture,
                const std::string& name);
    void resize(int w, int h);
    void destroy();

    void bind() const;
    static void unbind();

    GLuint colorTex(int i = 0) const { return colors_[i].tex; }
    GLuint depthTex() const { return depthTex_; }
    GLuint id() const { return fbo_; }
    int width() const { return w_; }
    int height() const { return h_; }
    const std::string& name() const { return name_; }
    const std::vector<Attachment>& colorAttachments() const { return colors_; }

private:
    void build();

    GLuint fbo_ = 0;
    GLuint depthTex_ = 0;
    bool useDepth_ = false;
    int w_ = 0, h_ = 0;
    std::string name_;
    std::vector<Attachment> colors_;
};
