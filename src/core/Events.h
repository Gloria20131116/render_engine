#pragma once
#include <string>

struct WindowResizeEvent {
    int width = 0, height = 0;
};

struct KeyEvent {
    int key = 0, action = 0, mods = 0;
};

struct FileDroppedEvent {
    std::string path;
};

// Fired by the file watcher when a watched file's timestamp changes.
struct FileChangedEvent {
    std::string path;
};

// Fired after a shader program successfully recompiled.
struct ShaderReloadedEvent {
    std::string name;
};
