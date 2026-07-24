#include "render/Shader.h"

#include <filesystem>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>
#include <sstream>
#include <vector>

#include "core/Log.h"

static std::string readFile(const std::string& path, bool& ok) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        ok = false;
        return {};
    }
    std::stringstream ss;
    ss << f.rdbuf();
    ok = true;
    return ss.str();
}

Shader::~Shader() {
    if (program_) glDeleteProgram(program_);
}

std::string Shader::preprocess(const std::string& path, int depth) {
    bool ok = false;
    std::string src = readFile(path, ok);
    if (!ok) {
        lastError_ += "Cannot open file: " + path + "\n";
        return {};
    }
    deps_.insert(std::filesystem::absolute(path).string());
    if (depth > 8) {
        lastError_ += "Include depth exceeded in " + path + "\n";
        return {};
    }

    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    std::stringstream out;
    std::istringstream in(src);
    std::string line;
    while (std::getline(in, line)) {
        size_t pos = line.find("#include");
        if (pos != std::string::npos && line.find_first_not_of(" \t") == pos) {
            size_t a = line.find('"', pos);
            size_t b = a == std::string::npos ? std::string::npos : line.find('"', a + 1);
            if (a != std::string::npos && b != std::string::npos) {
                std::string incName = line.substr(a + 1, b - a - 1);
                std::string incPath = (dir / incName).string();
                out << preprocess(incPath, depth + 1) << "\n";
                continue;
            }
        }
        out << line << "\n";
    }
    return out.str();
}

GLuint Shader::compileStage(GLenum type, const std::string& src, const std::string& label) {
    GLuint sh = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(sh, 1, &c, nullptr);
    glCompileShader(sh);
    GLint status = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
    if (!status) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(std::max(len, 1));
        glGetShaderInfoLog(sh, len, nullptr, log.data());
        lastError_ += label + ":\n" + log.data() + "\n";
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

bool Shader::load(const std::string& name, const std::string& vsPath, const std::string& fsPath) {
    name_ = name;
    vsPath_ = vsPath;
    fsPath_ = fsPath;
    return reload();
}

bool Shader::reload() {
    lastError_.clear();
    std::set<std::string> newDeps;
    std::swap(deps_, newDeps);  // deps_ now empty, rebuilt by preprocess

    std::string vsSrc = preprocess(vsPath_, 0);
    std::string fsSrc = preprocess(fsPath_, 0);
    if (!lastError_.empty()) {
        std::swap(deps_, newDeps);
        Log::error("Shader '%s' preprocess failed:\n%s", name_.c_str(), lastError_.c_str());
        return false;
    }

    GLuint vs = compileStage(GL_VERTEX_SHADER, vsSrc, vsPath_);
    GLuint fs = compileStage(GL_FRAGMENT_SHADER, fsSrc, fsPath_);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        std::swap(deps_, newDeps);  // restore old deps so watcher keeps working
        Log::error("Shader '%s' compile failed:\n%s", name_.c_str(), lastError_.c_str());
        return false;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint status = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &status);
    if (!status) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::vector<char> log(std::max(len, 1));
        glGetProgramInfoLog(prog, len, nullptr, log.data());
        lastError_ = std::string("link: ") + log.data();
        glDeleteProgram(prog);
        std::swap(deps_, newDeps);
        Log::error("Shader '%s' link failed:\n%s", name_.c_str(), lastError_.c_str());
        return false;
    }

    if (program_) glDeleteProgram(program_);
    program_ = prog;
    uniformCache_.clear();
    return true;
}

void Shader::bind() const { glUseProgram(program_); }

GLint Shader::location(const char* n) const {
    auto it = uniformCache_.find(n);
    if (it != uniformCache_.end()) return it->second;
    GLint loc = glGetUniformLocation(program_, n);
    uniformCache_[n] = loc;
    return loc;
}

void Shader::setInt(const char* n, int v) const { glUniform1i(location(n), v); }
void Shader::setFloat(const char* n, float v) const { glUniform1f(location(n), v); }
void Shader::setVec2(const char* n, const glm::vec2& v) const { glUniform2fv(location(n), 1, glm::value_ptr(v)); }
void Shader::setVec3(const char* n, const glm::vec3& v) const { glUniform3fv(location(n), 1, glm::value_ptr(v)); }
void Shader::setVec4(const char* n, const glm::vec4& v) const { glUniform4fv(location(n), 1, glm::value_ptr(v)); }
void Shader::setMat4(const char* n, const glm::mat4& v) const {
    glUniformMatrix4fv(location(n), 1, GL_FALSE, glm::value_ptr(v));
}
