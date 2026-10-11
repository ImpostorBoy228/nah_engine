#pragma once
#include "window/rgfw_shit.hpp"
#include <cstdio>
#include <cstdlib>

inline void fuck(const char* m) {
    fprintf(stderr, "%s\n", m);
}

class Window {
private:
    RGFW_window* win = nullptr;
public:
    Window(int w, int h) {
        if (!rgfw::init()) {
            fuck("wayland init failed");
            std::abort();
        }
        rgfw::WindowDesc desc = {};
        desc.title = "nah_engine";
        desc.width = w;
        desc.height = h;
        desc.resizable = true;
        desc.fullscreen = false;
        desc.borderless = false;
        desc.exit_on_escape = true;

        win = rgfw::createWindow(desc);
        if (win == nullptr) {
            fuck("window creation failed");
            rgfw::deinit();
            std::abort();
        }
    }

    ~Window() {
        if (win) rgfw::destroyWindow(win);
        rgfw::deinit();
    }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    RGFW_window* get() const { return win; }
    bool shouldClose() { return rgfw::shouldClose(win); }
};
