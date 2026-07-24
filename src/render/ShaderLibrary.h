#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/Shader.h"

class EventBus;
class FileWatcher;

// Owns all shader programs. Registers their source files (including #includes)
// with the FileWatcher and recompiles automatically when files change.
class ShaderLibrary {
public:
    void init(EventBus& bus, FileWatcher& watcher);

    // Names are relative to assets/shaders (e.g. "pbr.vert").
    std::shared_ptr<Shader> load(const std::string& name, const std::string& vsFile,
                                 const std::string& fsFile);
    std::shared_ptr<Shader> get(const std::string& name) const;

    const std::vector<std::shared_ptr<Shader>>& all() const { return list_; }

private:
    void watchDeps(const std::shared_ptr<Shader>& shader);
    void onFileChanged(const std::string& path);

    EventBus* bus_ = nullptr;
    FileWatcher* watcher_ = nullptr;
    std::unordered_map<std::string, std::shared_ptr<Shader>> shaders_;
    std::vector<std::shared_ptr<Shader>> list_;
};
