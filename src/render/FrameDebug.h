#pragma once
#include <GL/glew.h>

#include <string>
#include <unordered_map>
#include <vector>

#include "render/GpuTimer.h"

// RenderDoc-style bookkeeping: each render pass registers the textures it
// reads (inputs) and the render targets it writes (outputs) every frame,
// plus GPU time and draw call counts. The Frame Debugger UI reads this.
struct DebugResource {
    std::string label;   // e.g. "Shadow Map (depth)"
    GLuint tex = 0;
    GLenum target = GL_TEXTURE_2D;  // GL_TEXTURE_2D or GL_TEXTURE_CUBE_MAP
    int width = 0, height = 0;
    bool isDepth = false;
    bool singleChannel = false;
};

struct PassRecord {
    std::string name;
    std::string description;
    std::vector<DebugResource> inputs;
    std::vector<DebugResource> outputs;
    double gpuMs = 0.0;
    int drawCalls = 0;
    bool enabled = true;
};

class FrameDebugger {
public:
    void beginFrame() { passes_.clear(); }

    PassRecord& beginPass(const std::string& name, const std::string& description) {
        passes_.push_back({});
        PassRecord& p = passes_.back();
        p.name = name;
        p.description = description;
        timers_[name].begin();
        return p;
    }

    void endPass(const std::string& name) {
        timers_[name].end();
        for (auto& p : passes_)
            if (p.name == name) p.gpuMs = timers_[name].milliseconds();
    }

    const std::vector<PassRecord>& passes() const { return passes_; }

    double totalGpuMs() const {
        double t = 0.0;
        for (auto& p : passes_) t += p.gpuMs;
        return t;
    }

private:
    std::vector<PassRecord> passes_;
    std::unordered_map<std::string, GpuTimer> timers_;
};
