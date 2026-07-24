#pragma once
#include <GL/glew.h>

// Double-buffered GL_TIME_ELAPSED query: read last frame's result while
// recording this frame, so we never stall the pipeline.
class GpuTimer {
public:
    ~GpuTimer() {
        if (queries_[0]) glDeleteQueries(2, queries_);
    }

    void begin() {
        if (!queries_[0]) glGenQueries(2, queries_);
        if (framesRecorded_ >= 2) {
            GLuint64 ns = 0;
            glGetQueryObjectui64v(queries_[frame_ & 1], GL_QUERY_RESULT, &ns);
            lastMs_ = ns / 1.0e6;
        }
        glBeginQuery(GL_TIME_ELAPSED, queries_[frame_ & 1]);
        active_ = true;
    }

    void end() {
        if (!active_) return;
        glEndQuery(GL_TIME_ELAPSED);
        active_ = false;
        frame_++;
        if (framesRecorded_ < 2) framesRecorded_++;
    }

    double milliseconds() const { return lastMs_; }

private:
    GLuint queries_[2] = {0, 0};
    unsigned frame_ = 0;
    int framesRecorded_ = 0;
    bool active_ = false;
    double lastMs_ = 0.0;
};
