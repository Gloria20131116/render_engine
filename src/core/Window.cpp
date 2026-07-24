#include "core/Window.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "core/EventBus.h"
#include "core/Events.h"
#include "core/Log.h"

static void APIENTRY glDebugCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                     GLsizei, const GLchar* message, const void*) {
    (void)source;
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;  // driver noise
    if (type == GL_DEBUG_TYPE_ERROR)
        Log::error("[GL] %s", message);
    else if (severity == GL_DEBUG_SEVERITY_HIGH)
        Log::warn("[GL] %s", message);
}

bool Window::init(int width, int height, const std::string& title, EventBus& bus) {
    bus_ = &bus;
    if (!glfwInit()) {
        Log::error("Failed to init GLFW");
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);

    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        Log::error("Failed to create window (OpenGL 4.5 core required)");
        glfwTerminate();
        return false;
    }
    width_ = width;
    height_ = height;

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    GLenum err = glewInit();
    if (err != GLEW_OK) {
        Log::error("glewInit failed: %s", glewGetErrorString(err));
        return false;
    }
    glGetError();  // clear spurious error from glewInit

    Log::info("OpenGL %s | %s", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    if (GLEW_KHR_debug) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
        glDebugMessageCallback(glDebugCallback, nullptr);
    }

    glfwSetWindowUserPointer(window_, this);

    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* w, int width, int height) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        self->width_ = width;
        self->height_ = height;
        self->bus_->publish(WindowResizeEvent{width, height});
    });

    glfwSetKeyCallback(window_, [](GLFWwindow* w, int key, int, int action, int mods) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        self->bus_->publish(KeyEvent{key, action, mods});
    });

    glfwSetDropCallback(window_, [](GLFWwindow* w, int count, const char** paths) {
        auto* self = static_cast<Window*>(glfwGetWindowUserPointer(w));
        for (int i = 0; i < count; ++i) self->bus_->publish(FileDroppedEvent{paths[i]});
    });

    return true;
}

void Window::shutdown() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    glfwTerminate();
}

bool Window::shouldClose() const { return glfwWindowShouldClose(window_); }
void Window::pollEvents() { glfwPollEvents(); }
void Window::swapBuffers() { glfwSwapBuffers(window_); }
