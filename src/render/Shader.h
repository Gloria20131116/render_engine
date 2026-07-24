#pragma once
#include <GL/glew.h>

#include <glm/glm.hpp>
#include <set>
#include <string>
#include <unordered_map>

// A GL program built from vertex+fragment shader files.
// Supports `#include "file.glsl"` (relative to the shader directory) and
// keeps the previous program alive if a recompile fails.
class Shader {
public:
    ~Shader();

    // Paths are absolute. Returns false if the *initial* compile failed.
    bool load(const std::string& name, const std::string& vsPath, const std::string& fsPath);
    // Recompile from the same files. Keeps old program on failure.
    bool reload();

    void bind() const;
    GLuint id() const { return program_; }
    bool valid() const { return program_ != 0; }
    const std::string& name() const { return name_; }
    const std::string& lastError() const { return lastError_; }
    // Every file that participates in this program (sources + includes).
    const std::set<std::string>& dependencies() const { return deps_; }

    void setInt(const char* n, int v) const;
    void setFloat(const char* n, float v) const;
    void setVec2(const char* n, const glm::vec2& v) const;
    void setVec3(const char* n, const glm::vec3& v) const;
    void setVec4(const char* n, const glm::vec4& v) const;
    void setMat4(const char* n, const glm::mat4& v) const;

private:
    GLint location(const char* n) const;
    std::string preprocess(const std::string& path, int depth);
    GLuint compileStage(GLenum type, const std::string& src, const std::string& label);

    std::string name_, vsPath_, fsPath_;
    GLuint program_ = 0;
    std::set<std::string> deps_;
    std::string lastError_;
    mutable std::unordered_map<std::string, GLint> uniformCache_;
};
