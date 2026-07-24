#pragma once
#include <GL/glew.h>

// Shared dummy geometry for fullscreen / cube-capture passes.
class RenderUtil {
public:
    // Fullscreen triangle: vertices generated in the vertex shader from gl_VertexID.
    static void drawFullscreen() {
        ensure();
        glBindVertexArray(emptyVao());
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

    // 36-vertex cube, positions generated in shader from gl_VertexID.
    static void drawCube() {
        ensure();
        glBindVertexArray(emptyVao());
        glDrawArrays(GL_TRIANGLES, 0, 36);
        glBindVertexArray(0);
    }

private:
    static GLuint& emptyVao() {
        static GLuint vao = 0;
        return vao;
    }
    static void ensure() {
        if (!emptyVao()) glGenVertexArrays(1, &emptyVao());
    }
};
