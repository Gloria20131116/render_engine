#include "render/ShaderLibrary.h"

#include <filesystem>

#include "core/EventBus.h"
#include "core/Events.h"
#include "core/FileWatcher.h"
#include "core/Log.h"
#include "core/Paths.h"

void ShaderLibrary::init(EventBus& bus, FileWatcher& watcher) {
    bus_ = &bus;
    watcher_ = &watcher;
    bus.subscribe<FileChangedEvent>([this](const FileChangedEvent& e) { onFileChanged(e.path); });
}

std::shared_ptr<Shader> ShaderLibrary::load(const std::string& name, const std::string& vsFile,
                                            const std::string& fsFile) {
    auto shader = std::make_shared<Shader>();
    if (!shader->load(name, Paths::shader(vsFile), Paths::shader(fsFile))) {
        Log::error("Initial compile of shader '%s' failed", name.c_str());
    }
    shaders_[name] = shader;
    list_.push_back(shader);
    watchDeps(shader);
    return shader;
}

std::shared_ptr<Shader> ShaderLibrary::get(const std::string& name) const {
    auto it = shaders_.find(name);
    return it == shaders_.end() ? nullptr : it->second;
}

void ShaderLibrary::watchDeps(const std::shared_ptr<Shader>& shader) {
    for (const auto& dep : shader->dependencies()) watcher_->watch(dep);
}

void ShaderLibrary::onFileChanged(const std::string& path) {
    std::string abs = std::filesystem::absolute(path).string();
    for (auto& shader : list_) {
        if (shader->dependencies().count(abs) == 0) continue;
        if (shader->reload()) {
            Log::info("Reloaded shader '%s'", shader->name().c_str());
            watchDeps(shader);
            bus_->publish(ShaderReloadedEvent{shader->name()});
        }
    }
}
