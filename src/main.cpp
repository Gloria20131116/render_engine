#include <cstdio>
#include <cstring>

#include "core/Application.h"

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);

    Application app;
    // --screenshot <file.png> [frames]: render N frames, dump the window and exit.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            int frames = i + 2 < argc ? std::atoi(argv[i + 2]) : 30;
            app.requestScreenshot(argv[i + 1], frames > 0 ? frames : 30);
        }
    }

    if (!app.init()) return 1;
    app.run();
    app.shutdown();
    return 0;
}
