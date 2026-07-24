#include "render/Texture.h"

#include <filesystem>

#include "core/Log.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

Texture::~Texture() {
    if (id_) glDeleteTextures(1, &id_);
}

static std::shared_ptr<Texture> make2DFromPixels(const unsigned char* pixels, int w, int h,
                                                 int channels, bool srgb, const std::string& name) {
    GLenum internalFmt, fmt;
    switch (channels) {
        case 1: internalFmt = GL_R8; fmt = GL_RED; break;
        case 2: internalFmt = GL_RG8; fmt = GL_RG; break;
        case 3: internalFmt = srgb ? GL_SRGB8 : GL_RGB8; fmt = GL_RGB; break;
        default: internalFmt = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8; fmt = GL_RGBA; break;
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    return std::make_shared<Texture>(id, GL_TEXTURE_2D, w, h, name);
}

std::shared_ptr<Texture> Texture::load2D(const std::string& path, bool srgb) {
    stbi_set_flip_vertically_on_load(0);
    int w, h, n;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &n, 0);
    if (!pixels) {
        Log::error("Failed to load texture: %s (%s)", path.c_str(), stbi_failure_reason());
        return nullptr;
    }
    auto tex = make2DFromPixels(pixels, w, h, n, srgb,
                                std::filesystem::path(path).filename().string());
    stbi_image_free(pixels);
    tex->setSourcePath(path);
    Log::info("Loaded texture %s (%dx%d, %d ch)", path.c_str(), w, h, n);
    return tex;
}

std::shared_ptr<Texture> Texture::loadFromMemory(const unsigned char* data, int byteCount,
                                                 bool srgb, const std::string& name) {
    int w, h, n;
    unsigned char* pixels = stbi_load_from_memory(data, byteCount, &w, &h, &n, 0);
    if (!pixels) {
        Log::error("Failed to decode embedded texture '%s'", name.c_str());
        return nullptr;
    }
    auto tex = make2DFromPixels(pixels, w, h, n, srgb, name);
    stbi_image_free(pixels);
    return tex;
}

std::shared_ptr<Texture> Texture::loadHDR(const std::string& path) {
    stbi_set_flip_vertically_on_load(1);
    int w, h, n;
    float* pixels = stbi_loadf(path.c_str(), &w, &h, &n, 3);
    stbi_set_flip_vertically_on_load(0);
    if (!pixels) {
        Log::error("Failed to load HDR: %s (%s)", path.c_str(), stbi_failure_reason());
        return nullptr;
    }
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(pixels);
    auto tex = std::make_shared<Texture>(id, GL_TEXTURE_2D, w, h,
                                         std::filesystem::path(path).filename().string());
    tex->setSourcePath(path);
    Log::info("Loaded HDR environment %s (%dx%d)", path.c_str(), w, h);
    return tex;
}

std::shared_ptr<Texture> Texture::solid(unsigned char r, unsigned char g, unsigned char b,
                                        unsigned char a, const std::string& name) {
    unsigned char px[4] = {r, g, b, a};
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return std::make_shared<Texture>(id, GL_TEXTURE_2D, 1, 1, name);
}

std::shared_ptr<Texture> Texture::createCubemap(int size, GLenum internalFormat, bool mips,
                                                const std::string& name) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    GLenum fmt = GL_RGB;
    for (int i = 0; i < 6; ++i)
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, internalFormat, size, size, 0, fmt,
                     GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                    mips ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (mips) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
    return std::make_shared<Texture>(id, GL_TEXTURE_CUBE_MAP, size, size, name);
}

std::shared_ptr<Texture> Texture::create2D(int w, int h, GLenum internalFormat,
                                           const std::string& name) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, w, h);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return std::make_shared<Texture>(id, GL_TEXTURE_2D, w, h, name);
}

void Texture::bind(int unit) const {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(target_, id_);
}
