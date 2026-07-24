#pragma once
#include <string>

struct GLFWwindow;
class EventBus;

// GLFW window + OpenGL 4.5 core context. Forwards window events to the EventBus.
class Window {
public:
    bool init(int width, int height, const std::string& title, EventBus& bus);
    void shutdown();

    bool shouldClose() const;
    void pollEvents();
    void swapBuffers();

    int width() const { return width_; }
    int height() const { return height_; }
    GLFWwindow* handle() const { return window_; }

private:
    GLFWwindow* window_ = nullptr;
    EventBus* bus_ = nullptr;
    int width_ = 0, height_ = 0;
};
