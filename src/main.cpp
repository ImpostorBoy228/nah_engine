#include "window/rgfw_shit.hpp"

#include <stdio.h>

int main() {
    if (!rgfw::init()) {
        printf("wayland init failed\n");
        return 1;
    }

    rgfw::WindowDesc desc = {};
    desc.title = "nah_engine";
    desc.width = 1280;
    desc.height = 720;
    desc.resizable = true;
    desc.fullscreen = false;
    desc.borderless = false;
    desc.exit_on_escape = true;

    RGFW_window* win = rgfw::createWindow(desc);
    if (win == NULL) {
        printf("window creation failed\n");
        rgfw::deinit();
        return 1;
    }

    printf("wayland surface %p, display %p\n",
        (void*)rgfw::getSurface(win), (void*)rgfw::getDisplay());

    u32 extension_count = 0;
    const char* const* extensions = rgfw::getRequiredInstanceExtensions(&extension_count);
    for (u32 i = 0; i < extension_count; i++) {
        printf("instance extension %u: %s\n", i, extensions[i]);
    }

    rgfw::Event event = {};
    while (!rgfw::shouldClose(win)) {
        rgfw::waitEvent(win, 16);

        while (rgfw::nextEvent(&event)) {
            i32 width = 0;
            i32 height = 0;
            rgfw::getSize(win, &width, &height);

            switch (event.kind) {
                case rgfw::EventKind::resize:
                    printf("resize %dx%d\n", event.width, event.height);
                    break;
                case rgfw::EventKind::key_pressed:
                    printf("key %d pressed\n", (int)event.key);
                    break;
                case rgfw::EventKind::key_released:
                    printf("key %d released\n", (int)event.key);
                    break;
                case rgfw::EventKind::mouse_pressed:
                    printf("mouse %d pressed\n", (int)event.button);
                    break;
                case rgfw::EventKind::mouse_moved:
                    printf("mouse %d,%d\n", event.mouse_x, event.mouse_y);
                    break;
                case rgfw::EventKind::quit:
                    printf("quit\n");
                    break;
                default:
                    printf("event %d (%dx%d)\n", (int)event.kind, width, height);
                    break;
            }
        }
    }

    rgfw::destroyWindow(win);
    rgfw::deinit();
    return 0;
}