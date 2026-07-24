#pragma once
#include <GL/glew.h>

#include <memory>
#include <string>

// Thin wrapper over a GL texture object (2D or cubemap).
class Texture {
public:
    Texture() = default;
    Texture(GLuint id, GLenum target, int w, int h, std::string name)
        : id_(id), target_(target), width_(w), height_(h), name_(std::move(name)) {}
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    static std::shared_ptr<Texture> load2D(const std::string& path, bool srgb);
    static std::shared_ptr<Texture> loadFromMemory(const unsigned char* data, int byteCount,
                                                   bool srgb, const std::string& name);
    // Equirectangular .hdr file -> float texture.
    static std::shared_ptr<Texture> loadHDR(const std::string& path);
    // 1x1 solid color helper (RGBA 0..255).
    static std::shared_ptr<Texture> solid(unsigned char r, unsigned char g, unsigned char b,
                                          unsigned char a, const std::string& name);
    static std::shared_ptr<Texture> createCubemap(int size, GLenum internalFormat, bool mips,
                                                  const std::string& name);
    static std::shared_ptr<Texture> create2D(int w, int h, GLenum internalFormat,
                                             const std::string& name);

    void bind(int unit) const;
    GLuint id() const { return id_; }
    GLenum target() const { return target_; }
    int width() const { return width_; }
    int height() const { return height_; }
    const std::string& name() const { return name_; }
    const std::string& sourcePath() const { return sourcePath_; }
    void setSourcePath(std::string p) { sourcePath_ = std::move(p); }

private:
    GLuint id_ = 0;
    GLenum target_ = GL_TEXTURE_2D;
    int width_ = 0, height_ = 0;
    std::string name_;
    std::string sourcePath_;
};
